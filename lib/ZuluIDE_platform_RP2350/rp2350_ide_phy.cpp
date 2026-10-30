/**
 * ZuluIDE™ - Copyright (c) 2025 Rabbit Hole Computing™
 *
 * ZuluIDE™ firmware is licensed under the GPL version 3 or any later version. 
 *
 * https://www.gnu.org/licenses/gpl-3.0.html
 * ----
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version. 
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details. 
 *
 * Under Section 7 of GPL version 3, you are granted additional
 * permissions described in the ZuluIDE Hardware Support Library Exception
 * (GPL-3.0_HSL_Exception.md), as published by Rabbit Hole Computing™.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
**/


// This file implements interface to IDE / Parallel ATA bus.
// It communicates with the low level platform code running on second core.

#include "ZuluIDE_platform.h"
#include "ZuluIDE_config.h"
#include "ZuluIDE_log.h"
#include <hardware/gpio.h>
#include <hardware/pio.h>
#include <hardware/dma.h>
#include <hardware/structs/iobank0.h>
#include <assert.h>
#include "ide_constants.h"
#include "ide_phy.h"
#include "rp2350_sniffer.h"
#include "rp2350_iocs16.pio.h"

#include <zuluide_rp2350b_core1.h>

// PIO state machine used for IOC16 handling
// Must have IOBASE 16.
// The PIO is shared with SDIO and the offset is fixed to avoid
// problems when SDIO dynamically switches PIO programs, as they
// can take varying amount of space.
#ifndef IOCS16_PIO
#define IOCS16_PIO pio1
#define IOCS16_PIO_GPIO_FUNC GPIO_FUNC_PIO1
#endif
#ifndef IOCS16_PIO_SM
#define IOCS16_PIO_SM 3
#endif
#ifndef IOCS16_DMA_CH
#define IOCS16_DMA_CH 5
#endif
#ifndef IOCS16_PIO_PROG_OFFSET
#define IOCS16_PIO_PROG_OFFSET 0
#endif

static struct {
    ide_phy_config_t config;
    bool transfer_running; // Purely for debugging
    uint32_t transfer_block_start_time;
    volatile bool watchdog_error;

    // First ATA PIO data out block after command should
    // not assert IRQ, but further ones do.
    bool first_dataout_after_cmd;
    bool suppress_first_dataout_irq;

    // PIO address for iocs16 program
    bool pio_iocs16_claimed;

    uint32_t bufferidx; // Index of next buffer in g_idebuffers to use

    // Core1 command sequence number of the command being handled,
    // used to tag register updates sent to core1.
    uint32_t cmd_seq;

    // Core1 is tracking sectors of the current PIO data-in transfer
    bool datain_tracking;
} g_ide_phy;

static ide_phy_capabilities_t g_ide_phy_capabilities = {
    .max_blocksize = IDECOMM_MAX_BLOCK_PAYLOAD,
    .max_pio_mode = 3,
    .max_udma_mode = 2,
    .max_mwdma_mode = 2,
    .max_set_multiple = 8, // currently  IDECOMM_MAX_BLOCK_PAYLOAD / 512 (standard blocksize for rigid)
};

// Core1 uses atomic operations to access .requests and .events.
// When IDE_PIO->irq_force is set, core1 will respond to interrupt handler as
// soon as any higher priority IDE bus accesses are handled. The handler will
// process any pending requests in this order:
//
//  1. CORE1_REQ_SET_REGS       (Used for setting registers of active device)
//  2. CORE1_REQ_SET_REGS_DEV0  (Used for setting boot signature per-device)
//  3. CORE1_REQ_SET_REGS_DEV1  (Used for setting boot signature per-device)
//  4. CORE1_REQ_ASSERT_IRQ     (Set status and assert IRQ line)
//
//  5. CORE1_REQ_STOP_TRANSFERS (Stop data transfers, overrides any pending START_DATA requests)
//  6. CORE1_REQ_START_DATAIN   (Start PIO or UDMA data in transfer, set status, assert IRQ)
//  7. CORE1_REQ_START_DATAOUT  (Start PIO or UDMA data out transfer, set status, assert IRQ)
//  8. CORE1_REQ_START_DATAOUT_NOIRQ (Start PIO or UDMA data out transfer, set status but do not assert IRQ)
//
//  9. CORE1_REQ_SET_SIGNALS    (Set the low-speed DASP/PDIAG signals)
// 10. CORE1_REQ_GET_SIGNALS    (Read DASP/PDIAG signal states)
//
// 11. CORE1_REQ_CHANGE_PIO_MODE  (Update PIO mode from g_idecomm.pio_mode)
// 12. CORE1_REQ_PRINT_DEBUG    (Print core1 debug information such as RP2350 DMA and PIO regs)
//
// The processing order ensures that e.g. CORE1_REQ_SET_REGS and CORE1_REQ_START_DATAIN can
// be queued and will be processed in correct order. In IDE protocol, the device should change
// register values only when the status is busy, and host will read them only after status is
// set to not-busy
//
// In general core1 processes requests within 1µs. The CORE1_REQ_BUSY indicates a state where
// the request bit has been cleared from g_idecomm.requests but the interrupt handler has not
// yet fully completed. New requests can be queued also during this state.

static void ide_phy_post_request(uint32_t request)
{
    __atomic_or_fetch(&g_idecomm.requests, request, __ATOMIC_ACQ_REL);
    IDE_PIO->irq_force = (1 << IDE_CORE1_WAKEUP_IRQ);
}

static void ide_phy_clear_event(uint32_t event)
{
    __atomic_fetch_and(&g_idecomm.events, ~event, __ATOMIC_ACQ_REL);
}

// Take the command sequence number of the command or reset that is now being handled.
// Must be called after the corresponding event has been cleared, so that a newer command
// sets the event again and gets handled next.
static void ide_phy_update_cmd_seq()
{
    g_ide_phy.cmd_seq = __atomic_load_n(&g_idecomm.cmd_seq, __ATOMIC_ACQUIRE);
}

// Check if host has written a new command or reset after the one core0 is handling.
// Transfers of the old command must then not be set up, core1 may already be receiving
// the ATAPI packet of the new command.
static bool ide_phy_new_command_pending()
{
    return __atomic_load_n(&g_idecomm.cmd_seq, __ATOMIC_ACQUIRE) != g_ide_phy.cmd_seq;
}

// Set the data block size for a new transfer, unless host has started a new command.
// Core1 sets the block size of the ATAPI packet itself when host writes a PACKET
// command, and that must not be overwritten by a transfer of the previous command.
static bool ide_phy_set_blocksize(uint32_t blocklen)
{
    if (ide_phy_new_command_pending())
    {
        return false;
    }

    g_idecomm.datablocksize = blocklen;
    __sync_synchronize();

    if (ide_phy_new_command_pending())
    {
        // Host wrote a new command while the block size was being set, so core1 may have set
        // the ATAPI packet size in between. Core1 stores the command before incrementing
        // cmd_seq, so the command register value is valid here.
        if (g_idecomm.phyregs.regs.command == IDE_CMD_PACKET)
        {
            g_idecomm.datablocksize = 12;
        }
        return false;
    }

    return true;
}

// Post a request that belongs to the command core0 is currently handling.
// Core1 drops it if host has started a new command before it is processed.
static void ide_phy_post_command_request(uint32_t request)
{
    g_idecomm.set_regs_seq = g_ide_phy.cmd_seq;
    __sync_synchronize();
    ide_phy_post_request(request);
}

static bool ide_phy_check_pending_request(uint32_t request)
{
    // Use atomic_load_n() to make sure compiler doesn't optimize away
    // repeated reads.
    return __atomic_load_n(&g_idecomm.requests, __ATOMIC_ACQUIRE) & request;
}

void core1_log_poll();

void ide_phy_config(const ide_phy_config_t* config)
{
    if (g_rp2350_passive_sniffer) return;

    g_idecomm.enable_idephy = false;
    busy_wait_us_32(CORE1_RESPONSE_DELAY);

    if (!g_ide_phy.pio_iocs16_claimed)
    {
        pio_sm_claim(IOCS16_PIO, IOCS16_PIO_SM);
        pio_add_program_at_offset(IOCS16_PIO, &rp2350_iocs16_program, IOCS16_PIO_PROG_OFFSET);
        dma_channel_claim(IOCS16_DMA_CH);
        g_idecomm.iocs16_dma_channel = IOCS16_DMA_CH;
        g_idecomm.iocs16_dma_target_addr = (uint32_t)&IOCS16_PIO->txf[IOCS16_PIO_SM];
        g_ide_phy.pio_iocs16_claimed = true;
    }

    // Configure the PIO block that is used for IOCS16 signal handling
    if (config->disable_iocs16)
    {
        g_idecomm.enable_iocs16 = false;
        gpio_set_outover(IDE_IOCS16, GPIO_OVERRIDE_HIGH);
        dbgmsg("IOCS16 signaling for PIO data transfers is disabled");
    }
    else
    {
        assert(IOCS16_PIO->gpiobase == 16);
        g_idecomm.enable_iocs16 = true;
        pio_sm_config cfg = rp2350_iocs16_program_get_default_config(IOCS16_PIO_PROG_OFFSET);
        sm_config_set_set_pins(&cfg, IDE_IOCS16, 1);
        pio_sm_set_enabled(IOCS16_PIO, IOCS16_PIO_SM, false);
        pio_sm_init(IOCS16_PIO, IOCS16_PIO_SM, IOCS16_PIO_PROG_OFFSET, &cfg);
        pio_sm_put(IOCS16_PIO, IOCS16_PIO_SM, 0x40); // Data register address
        pio_sm_exec(IOCS16_PIO, IOCS16_PIO_SM, pio_encode_pull(false, false));
        pio_sm_exec(IOCS16_PIO, IOCS16_PIO_SM, pio_encode_out(pio_y, 8));
        pio_sm_set_consecutive_pindirs(IOCS16_PIO, IOCS16_PIO_SM, IDE_IOCS16, 1, true);
        pio_sm_set_enabled(IOCS16_PIO, IOCS16_PIO_SM, true);
        gpio_set_function(IDE_IOCS16, IOCS16_PIO_GPIO_FUNC);
        gpio_set_outover(IDE_IOCS16, GPIO_OVERRIDE_NORMAL);
        dbgmsg("Enabled IOCS16 signaling");
    }

    // Only initialize registers once after boot, after that ide_protocol handles it.
    static bool regs_inited = false;
    phy_ide_registers_t phyregs = g_idecomm.phyregs;

    if (!regs_inited)
    {
#ifdef ZULUIDE_RP2350B_CORE1_HAVE_SOURCE
        zuluide_rp2350b_core1_run();
        delay(200);
#endif

        memset(&phyregs, 0, sizeof(phyregs));
        regs_inited = true;
    }
    __sync_synchronize();

    g_ide_phy.config = *config;
    g_ide_phy.watchdog_error = false;

    g_idecomm.enable_dev0          = config->enable_dev0;
    g_idecomm.enable_dev1          = config->enable_dev1;
    g_idecomm.enable_dev1_zeros    = config->enable_dev1_zeros;
    g_idecomm.atapi_dev0           = config->atapi_dev0;
    g_idecomm.atapi_dev1           = config->atapi_dev1;
    g_idecomm.disable_iordy        = config->disable_iordy;
    g_idecomm.enable_packet_intrq  = config->enable_packet_intrq;
    g_idecomm.cpu_freq_hz = clock_get_hz(clk_sys);
    phyregs.state_irqreq = 0;
    phyregs.state_datain = 0;
    phyregs.state_dataout = 0;
    g_idecomm.phyregs = phyregs;
    g_idecomm.set_regs = phyregs.regs;
    ide_phy_update_cmd_seq();
    // Not tied to a host command, core1 must apply these even if the host
    // starts a new command or holds the bus in reset meanwhile.
    g_idecomm.set_regs_seq = IDECOMM_SEQ_ANY;
    __sync_synchronize();
    ide_phy_post_request(CORE1_REQ_SET_REGS | CORE1_REQ_STOP_TRANSFERS);

    g_idecomm.enable_idephy = true;

    busy_wait_us_32(CORE1_RESPONSE_DELAY);
    core1_log_poll();

    if (ide_phy_check_pending_request(CORE1_REQ_SET_REGS))
    {
        logmsg("ide_phy_config: Core1 is not responding.");
        ide_phy_print_debug();
    }
}

void ide_phy_reset()
{
    if (g_rp2350_passive_sniffer) return;

    g_idecomm.enable_idephy = false;
    busy_wait_us_32(CORE1_RESPONSE_DELAY);
    phy_ide_registers_t phyregs = g_idecomm.phyregs;
    __sync_synchronize();

    g_ide_phy.watchdog_error = false;

    phyregs.state_irqreq = 0;
    phyregs.state_datain = 0;
    phyregs.state_dataout = 0;
    g_idecomm.phyregs = phyregs;
    g_idecomm.set_regs = phyregs.regs;
    ide_phy_update_cmd_seq();
    // Not tied to a host command, core1 must apply these even if the host
    // starts a new command or holds the bus in reset meanwhile.
    g_idecomm.set_regs_seq = IDECOMM_SEQ_ANY;
    __sync_synchronize();
    ide_phy_post_request(CORE1_REQ_SET_REGS | CORE1_REQ_STOP_TRANSFERS);

    // Drain FIFO
    while (sio_hw->fifo_st & SIO_FIFO_ST_VLD_BITS)
    {
        (void)sio_hw->fifo_rd;
    }

    g_idecomm.enable_idephy = true;

    busy_wait_us_32(CORE1_RESPONSE_DELAY);
    core1_log_poll();

    if (ide_phy_check_pending_request(CORE1_REQ_SET_REGS))
    {
        logmsg("ide_phy_reset: Core1 is not responding.");
        ide_phy_print_debug();
    }

}

void ide_phy_reset_from_watchdog()
{
    g_ide_phy.watchdog_error = true;
}

extern void core1_log_poll();

void ide_phy_print_debug()
{
    if (!g_log_debug) return;

    ide_phy_post_request(CORE1_REQ_PRINT_DEBUG);

    dbgmsg("Transfer running: ", (int)g_ide_phy.transfer_running, ", watchdog error: ", (int)g_ide_phy.watchdog_error);
    dbgmsg("UDMA: ", g_idecomm.udma_mode, " MWDMA: ", g_idecomm.mwdma_mode, " Checksum errors: ", g_idecomm.udma_checksum_errors);
    dbgmsg("Core1 requests: ", g_idecomm.requests, " events: ", g_idecomm.events);
    ide_registers_t regs = g_idecomm.phyregs.regs;
    dbgmsg("IDE regs:",
        " STATUS:", regs.status,
        " CMD:", regs.command,
        " DEV:", regs.device,
        " DEVCTRL:", regs.device_control,
        " ERROR:", regs.error,
        " FEATURE:", regs.feature,
        " LBAL:", regs.lba_low,
        " LBAM:", regs.lba_mid,
        " LBAH:", regs.lba_high);
    dbgmsg("IRQReq: ", (int)(g_idecomm.phyregs.state_irqreq != 0),
           " Datain: ", (int)(g_idecomm.phyregs.state_datain != 0),
           " Dataout: ", (int)(g_idecomm.phyregs.state_dataout != 0));
    dbgmsg("GPIO in: ", sio_hw->gpio_in, " out: ", sio_hw->gpio_out, " oe: ", sio_hw->gpio_oe);

    core1_log_poll();
}

ide_event_t ide_phy_get_events()
{
    uint32_t flags = g_idecomm.events;

    if (flags & CORE1_EVT_HWRST)
    {
        ide_phy_clear_event(CORE1_EVT_HWRST);
        busy_wait_us_32(CORE1_RESPONSE_DELAY);
        if (g_idecomm.events & CORE1_EVT_HWRST)
        {
            // Reset still continues, report when it ends
            return IDE_EVENT_NONE;
        }
        else
        {
            ide_phy_update_cmd_seq();
            return IDE_EVENT_HWRST;
        }
    }
    else if ((flags & CORE1_EVT_SWRST) || g_ide_phy.watchdog_error)
    {
        // Software reset
        ide_phy_clear_event(CORE1_EVT_SWRST);
        ide_phy_update_cmd_seq();
        g_ide_phy.watchdog_error = false;
        return IDE_EVENT_SWRST;
    }
    else if (flags & CORE1_EVT_CMD_RECEIVED)
    {
        // dbgmsg("IDE_EVENT_CMD, status ", g_idecomm.phyregs.regs.status);
        ide_phy_clear_event(CORE1_EVT_CMD_RECEIVED);
        ide_phy_update_cmd_seq();
        g_idecomm.udma_mode = -1; // For ATAPI packets
        g_idecomm.mwdma_mode = -1;
        g_ide_phy.transfer_block_start_time = millis();
        g_ide_phy.first_dataout_after_cmd = true;
        g_ide_phy.suppress_first_dataout_irq = false;
        return IDE_EVENT_CMD;
    }
    // Not used currently by application code
    // else if (flags & CORE1_EVT_DATA_DONE)
    // {
    //     ide_phy_clear_event(CORE1_EVT_DATA_DONE);
    //     return IDE_EVENT_DATA_TRANSFER_DONE;
    // }

    return IDE_EVENT_NONE;
}

bool ide_phy_is_command_interrupted()
{
    if (g_ide_phy.watchdog_error)
    {
        dbgmsg("Command interrupted because of watchdog error");
        return true;
    }
    else if (g_idecomm.events & (CORE1_EVT_CMD_RECEIVED | CORE1_EVT_HWRST | CORE1_EVT_SWRST))
    {
        dbgmsg("Command interrupted because of events ", g_idecomm.events);
        return true;
    }

    return false;
}

void ide_phy_get_regs(ide_registers_t *regs)
{
    // Wait for any outstanding register update to complete before reading them back
    uint32_t start = time_us_32();
    uint32_t reqs = CORE1_REQ_BUSY | CORE1_REQ_SET_REGS | CORE1_REQ_SET_REGS_DEV0 | CORE1_REQ_SET_REGS_DEV1;
    while (ide_phy_check_pending_request(reqs))
    {
        if ((uint32_t)(time_us_32() - start) > CORE1_RESPONSE_DELAY)
        {
            logmsg("ide_phy_get_regs: Core1 is not responding");
            ide_phy_print_debug();
            break;
        }
    }

    *regs = g_idecomm.phyregs.regs;
    // dbgmsg("GET_REGS, status ", regs->status, " error ", regs->error, " lba_high ", regs->lba_high);
}

void ide_phy_set_regs(const ide_registers_t *regs, int device)
{
    g_idecomm.set_regs = *regs;
    g_idecomm.set_regs_seq = g_ide_phy.cmd_seq;
    __sync_synchronize();

    if (device < 0)
        ide_phy_post_request(CORE1_REQ_SET_REGS); // Set for currently active device
    else if (device == 0)
        ide_phy_post_request(CORE1_REQ_SET_REGS_DEV0);
    else if (device == 1)
        ide_phy_post_request(CORE1_REQ_SET_REGS_DEV1);
    else
        logmsg("ide_phy_set_regs: Invalid device index ", device);
    
    // dbgmsg("SET_REGS dev ", device, ", status ", regs->status, " error ", regs->error, " lba_high ", regs->lba_high, " data_in ", (int)g_idecomm.phyregs.state_datain);
}

void ide_phy_set_pio_mode(int pio_mode)
{
    // Core1 uses different timing mode to support newer hosts with PIO mode >= 3.
    // Both timings work for the vast majority of devices, but a slightly different
    // handling of IORDY improves compatibility.
    if (pio_mode != g_idecomm.pio_mode)
    {
        g_idecomm.pio_mode = pio_mode;
        ide_phy_post_request(CORE1_REQ_CHANGE_PIO_MODE);
    }
}

// Set the data transfer mode for core1, UDMA takes precedence over Multiword DMA
static void ide_phy_set_dma_mode(int udma_mode, int mwdma_mode)
{
    g_idecomm.udma_mode = udma_mode;
    g_idecomm.mwdma_mode = (udma_mode >= 0) ? -1 : mwdma_mode;
}

void ide_phy_start_write(uint32_t blocklen, int udma_mode, int mwdma_mode)
{
    if (blocklen & 1) blocklen++;

    g_ide_phy.datain_tracking = false;
    if (!ide_phy_set_blocksize(blocklen))
    {
        dbgmsg("ide_phy_start_write(): host has started a new command, not starting transfer");
        g_ide_phy.transfer_running = false;
        return;
    }

    ide_phy_set_dma_mode(udma_mode, mwdma_mode);
    g_idecomm.udma_checksum_errors = 0;
    g_idecomm.datain_sectors_remaining = 0;
    g_ide_phy.transfer_running = true;

    // Actual write is started when first block is written
}

void ide_phy_start_ata_write(uint32_t blocklen, int udma_mode, uint32_t total_sectors, uint32_t sectors_per_block)
{
    ide_phy_start_write(blocklen, udma_mode);

    // Sector tracking is done by core1 at the end of each PIO data in block
    if (g_ide_phy.transfer_running && udma_mode < 0 && sectors_per_block > 0 && total_sectors > 0)
    {
        g_idecomm.datain_sectors_per_block = sectors_per_block;
        g_idecomm.datain_sectors_remaining = total_sectors;
        g_ide_phy.datain_tracking = true;
    }
}

bool ide_phy_is_ata_write_complete()
{
    // Core1 counts datain_sectors_remaining down to zero only when the last sector has
    // been transferred, so this is false if host interrupted the transfer.
    // With a core1 binary that does not support tracking, this stays non-zero.
    return g_ide_phy.datain_tracking &&
           __atomic_load_n(&g_idecomm.datain_sectors_remaining, __ATOMIC_ACQUIRE) == 0;
}

bool ide_phy_can_write_block()
{
    return (sio_hw->fifo_st & SIO_FIFO_ST_RDY_BITS);
}

// Get a block pointer that can be used for transmitting or receiving.
// The block is aligned so that it always ends at buffer boundary.
// This alignment is used by the core1 code.
static uint8_t *get_block_pointer()
{
    uint8_t *block = g_idebuffers[g_ide_phy.bufferidx++ % IDECOMM_BUFFERCOUNT];

    if (g_idecomm.udma_mode < 0)
    {
        // In PIO and Multiword DMA modes the data is packed into 32 bits per each 16 bit word
        block += IDECOMM_MAX_BLOCKSIZE - g_idecomm.datablocksize * 2;
    }
    else
    {
        // In UDMA mode the data is as-is.
        block += IDECOMM_MAX_BLOCKSIZE - g_idecomm.datablocksize;
    }

    return block;
}

void ide_phy_write_block(const uint8_t *buf, uint32_t blocklen)
{
    if (blocklen & 1) blocklen++;

    if (ide_phy_new_command_pending())
    {
        // Caller notices the new command from ide_phy_is_command_interrupted()
        dbgmsg("ide_phy_write_block(): host has started a new command, not sending data block");
        return;
    }

    assert(blocklen == g_idecomm.datablocksize);
    assert(sio_hw->fifo_st & SIO_FIFO_ST_RDY_BITS);

    // Copy data to a block that remains valid for duration of transfer
    uint8_t *block = get_block_pointer();

    if (g_idecomm.udma_mode < 0)
    {
        // PIO and Multiword DMA data requires special formatting
        const uint16_t *src = (const uint16_t*)buf;
        uint32_t *dst = (uint32_t*)block;
        uint32_t count = blocklen / 2;
        while (count >= 4)
        {
            *dst++ = IDECOMM_DATAFORMAT_PIO(*src++);
            *dst++ = IDECOMM_DATAFORMAT_PIO(*src++);
            *dst++ = IDECOMM_DATAFORMAT_PIO(*src++);
            *dst++ = IDECOMM_DATAFORMAT_PIO(*src++);
            count -= 4;
        }
        while (count > 0)
        {
            *dst++ = IDECOMM_DATAFORMAT_PIO(*src++);
            count--;
        }
    }
    else
    {
        // UDMA data can be copied directly
        memcpy(block, buf, blocklen);
    }

    // dbgmsg("Write block ptr ", (uint32_t)block, " length ", (int)blocklen, " udma ", g_idecomm.udma_mode);

    ide_phy_clear_event(CORE1_EVT_DATA_DONE);

    // Give the transmit pointer to core 1
    sio_hw->fifo_wr = (uint32_t)block;

    ide_phy_post_command_request(CORE1_REQ_START_DATAIN);
    g_ide_phy.transfer_block_start_time = millis();
}

bool ide_phy_is_write_finished()
{
    uint32_t requests = g_idecomm.requests;
    if ((requests & CORE1_REQ_START_DATAIN) || (requests & CORE1_REQ_BUSY))
    {
        return false; // Busy starting the data in request
    }

    return !g_idecomm.phyregs.state_datain;
}

static void data_out_give_next_block(bool noirq)
{
    if (ide_phy_new_command_pending())
    {
        // Caller notices the new command from ide_phy_is_command_interrupted()
        dbgmsg("data_out_give_next_block(): host has started a new command, not receiving data block");
        return;
    }

    // Select a buffer for the transfer
    uint8_t *block = get_block_pointer();

    // Tell core1 to receive the block
    assert(sio_hw->fifo_st & SIO_FIFO_ST_RDY_BITS);
    sio_hw->fifo_wr = (uint32_t)block;

    if (g_ide_phy.suppress_first_dataout_irq && g_ide_phy.first_dataout_after_cmd)
    {
        ide_phy_post_command_request(CORE1_REQ_START_DATAOUT_NOIRQ);
    }
    else
    {
        ide_phy_post_command_request(CORE1_REQ_START_DATAOUT);
    }

    g_ide_phy.first_dataout_after_cmd = false;
    g_ide_phy.transfer_block_start_time = millis();
}

void ide_phy_start_read(uint32_t blocklen, int udma_mode, int mwdma_mode)
{
    if (blocklen & 1) blocklen++;

    if (!ide_phy_set_blocksize(blocklen))
    {
        dbgmsg("ide_phy_start_read(): host has started a new command, not starting transfer");
        g_ide_phy.transfer_running = false;
        return;
    }

    ide_phy_set_dma_mode(udma_mode, mwdma_mode);
    g_idecomm.udma_checksum_errors = 0;
    g_ide_phy.transfer_running = true;

    data_out_give_next_block(false);
}

void ide_phy_start_ata_read(uint32_t blocklen, int udma_mode, int mwdma_mode)
{
    if (udma_mode < 0 && mwdma_mode < 0)
    {
        // For ATA transfers, first PIO DATA OUT block should not assert IRQ.
        // Further blocks should assert IRQ.
        // See figure 28 in T13/1410D revision 3a
        g_ide_phy.suppress_first_dataout_irq = true;
    }

    ide_phy_start_read(blocklen, udma_mode, mwdma_mode);
}

bool ide_phy_can_read_block()
{
    bool status = sio_hw->fifo_st & SIO_FIFO_ST_VLD_BITS;

    if (!status && (uint32_t)(millis() - g_ide_phy.transfer_block_start_time) > 5000)
    {
        if (!g_ide_phy.watchdog_error)
        {
            logmsg("ide_phy_can_read_block() detected transfer timeout");
            g_ide_phy.watchdog_error = true;
        }
    }

    return status;
}

void ide_phy_start_read_buffer(uint32_t blocklen)
{
    ide_phy_start_read(blocklen);
}

void ide_phy_read_block(uint8_t *buf, uint32_t blocklen, bool continue_transfer)
{
    uint32_t blocklen_even = blocklen;
    if (blocklen & 1) blocklen_even++;
    assert(blocklen_even == g_idecomm.datablocksize);
    assert(sio_hw->fifo_st & SIO_FIFO_ST_VLD_BITS);
    const uint32_t *rxbuf = (const uint32_t*)sio_hw->fifo_rd;

    if (continue_transfer)
    {
        // Next block reception can be started immediately
        data_out_give_next_block(false);
    }

    if (g_idecomm.udma_mode < 0)
    {
        // TODO: unroll
        // Narrowing conversion from 32 bits, for PIO and Multiword DMA data
        uint16_t *dst = (uint16_t*)buf;
        for (int i = 0; i < blocklen / 2; i++)
        {
            *dst++ = *rxbuf++;
        }
    }
    else
    {
        // UDMA data can be copied directly
        memcpy(buf, rxbuf, blocklen);
    }
}

void ide_phy_ata_read_block(uint8_t *buf, uint32_t blocklen, bool continue_transfer)
{
    ide_phy_read_block(buf, blocklen, continue_transfer);
}

void ide_phy_stop_transfers(int *crc_errors)
{
    g_ide_phy.transfer_running = false;
    g_ide_phy.datain_tracking = false;

    if (__atomic_load_n(&g_idecomm.cmd_seq, __ATOMIC_ACQUIRE) != g_ide_phy.cmd_seq)
    {
        // Host has already issued a new command or reset, and core1 has stopped the transfers
        // of this command itself. Core1 may have already received the new command's ATAPI
        // packet into the FIFO and set datablocksize for it, so leave the shared state alone.
        dbgmsg("ide_phy_stop_transfers(): new command pending, not stopping its transfers");
    }
    else
    {
        // Core1 drops the stop request if host issues a new command before it is processed
        g_idecomm.set_regs_seq = g_ide_phy.cmd_seq;
        __sync_synchronize();
        ide_phy_post_request(CORE1_REQ_STOP_TRANSFERS);

        // Wait for core1 to handle the stop before returning. Core1 skips start requests that
        // are in the same batch as a stop, so a transfer started right after this would be
        // silently dropped and the command completed without data. This happens when core0
        // responds to an ATAPI packet while core1 is still in the register write handler.
        uint32_t start = time_us_32();
        while (ide_phy_check_pending_request(CORE1_REQ_STOP_TRANSFERS | CORE1_REQ_BUSY))
        {
            if ((uint32_t)(time_us_32() - start) > CORE1_RESPONSE_DELAY)
            {
                logmsg("ide_phy_stop_transfers: Core1 is not responding");
                ide_phy_print_debug();
                break;
            }
        }

        ide_phy_clear_event(CORE1_EVT_DATA_DONE);

        if (ide_phy_set_blocksize(0))
        {
            // Drain FIFO
            while (sio_hw->fifo_st & SIO_FIFO_ST_VLD_BITS)
            {
                (void)sio_hw->fifo_rd;
            }
        }
        else
        {
            // Host wrote a new command just now. Its ATAPI packet will arrive in the FIFO.
            dbgmsg("ide_phy_stop_transfers(): new command arrived while stopping, not draining FIFO");
        }
    }

    if (crc_errors) *crc_errors = g_idecomm.udma_checksum_errors;
}

void ide_phy_assert_irq(uint8_t ide_status)
{
    g_idecomm.set_regs.status = ide_status;
    g_idecomm.set_regs_seq = g_ide_phy.cmd_seq;
    __sync_synchronize();

    ide_phy_post_request(CORE1_REQ_ASSERT_IRQ);
    // dbgmsg("ASSERT_IRQ, status ", ide_status);
}

void ide_phy_set_signals(uint8_t signals)
{
    if (signals != g_idecomm.set_signals)
    {
        g_idecomm.set_signals = signals;
        ide_phy_post_request(CORE1_REQ_SET_SIGNALS);
    }
}

uint8_t ide_phy_get_signals()
{
    static uint32_t last_poll = 0;

    // The DASP and PDIAG signals are held for several seconds
    // so we don't need to poll them that often.
    uint32_t time_now = millis();
    if ((uint32_t)(time_now - last_poll) > 50)
    {
        last_poll = time_now;
        ide_phy_post_request(CORE1_REQ_GET_SIGNALS);
        busy_wait_us_32(CORE1_RESPONSE_DELAY);
    }

    return g_idecomm.get_signals;
}

const ide_phy_capabilities_t *ide_phy_get_capabilities()
{
    return &g_ide_phy_capabilities;
}
