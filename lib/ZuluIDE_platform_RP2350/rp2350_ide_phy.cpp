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

#include <zuluide_rp2350b_core1.h>

static struct {
    ide_phy_config_t config;
    bool transfer_running; // Purely for debugging
    uint32_t transfer_block_start_time;
    volatile bool watchdog_error;

    uint32_t bufferidx; // Index of next buffer in g_idebuffers to use
} g_ide_phy;

static ide_phy_capabilities_t g_ide_phy_capabilities = {
    .max_blocksize = IDECOMM_MAX_BLOCK_PAYLOAD,

    .supports_iordy = true,
    .max_pio_mode = 3,
    .min_pio_cycletime_no_iordy = 240,
    .min_pio_cycletime_with_iordy = 180,

    .max_udma_mode = 2,
};

static void ide_phy_post_request(uint32_t request)
{
    __atomic_or_fetch(&g_idecomm.requests, request, __ATOMIC_ACQ_REL);
    IDE_PIO->irq_force = (1 << IDE_CORE1_WAKEUP_IRQ);
}

static void ide_phy_clear_event(uint32_t event)
{
    __atomic_fetch_and(&g_idecomm.events, ~event, __ATOMIC_ACQ_REL);
}

void core1_log_poll();

void ide_phy_reset(const ide_phy_config_t* config)
{
    if (g_rp2350_passive_sniffer) return;

    g_idecomm.enable_idephy = false;
    delay(2);

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

    // dbgmsg("ide_phy_reset");

    g_idecomm.enable_dev0          = config->enable_dev0;
    g_idecomm.enable_dev1          = config->enable_dev1;
    g_idecomm.enable_dev1_zeros    = config->enable_dev1_zeros;
    g_idecomm.atapi_dev0           = config->atapi_dev0;
    g_idecomm.atapi_dev1           = config->atapi_dev1;
    g_idecomm.disable_iordy        = config->disable_iordy;
    g_idecomm.enable_packet_intrq  = config->enable_packet_intrq;
    g_idecomm.disable_iocs16       = config->disable_iocs16;
    if (g_idecomm.disable_iocs16) dbgmsg("IOCS16 signaling for PIO data transfers is disabled");
    g_idecomm.cpu_freq_hz = clock_get_hz(clk_sys);
    phyregs.state_irqreq = 0;
    phyregs.state_datain = 0;
    phyregs.state_dataout = 0;
    g_idecomm.phyregs = phyregs;
    ide_phy_post_request(CORE1_REQ_SET_REGS | CORE1_REQ_STOP_TRANSFERS);

    g_idecomm.enable_idephy = true;

    delay(2);
    core1_log_poll();

    if (g_idecomm.requests & CORE1_REQ_SET_REGS)
    {
        logmsg("ERROR: Core1 is not responding.");
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
    dbgmsg("UDMA: ", g_idecomm.udma_mode, " Checksum errors: ", g_idecomm.udma_checksum_errors);
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
        delay(1);
        if (g_idecomm.events & CORE1_EVT_HWRST)
        {
            // Reset still continues, report when it ends
            return IDE_EVENT_NONE;
        }
        else
        {
            return IDE_EVENT_HWRST;
        }
    }
    else if ((flags & CORE1_EVT_SWRST) || g_ide_phy.watchdog_error)
    {
        // Software reset
        ide_phy_clear_event(CORE1_EVT_SWRST);
        g_ide_phy.watchdog_error = false;
        return IDE_EVENT_SWRST;
    }
    else if (flags & CORE1_EVT_CMD_RECEIVED)
    {
        // dbgmsg("IDE_EVENT_CMD, status ", g_idecomm.phyregs.regs.status);
        ide_phy_clear_event(CORE1_EVT_CMD_RECEIVED);
        g_idecomm.udma_mode = -1; // For ATAPI packets
        g_ide_phy.transfer_block_start_time = millis();
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
    return g_ide_phy.watchdog_error || (g_idecomm.events & (CORE1_EVT_CMD_RECEIVED | CORE1_EVT_HWRST | CORE1_EVT_SWRST));
}

void ide_phy_get_regs(ide_registers_t *regs)
{
    *regs = g_idecomm.phyregs.regs;
    // dbgmsg("GET_REGS, status ", regs->status, " error ", regs->error, " lba_high ", regs->lba_high);
}

void ide_phy_set_regs(const ide_registers_t *regs)
{
    g_idecomm.phyregs.regs = *regs;
    __sync_synchronize();

    ide_phy_post_request(CORE1_REQ_SET_REGS);
    // dbgmsg("SET_REGS, status ", regs->status, " error ", regs->error, " lba_high ", regs->lba_high, " data_in ", (int)g_idecomm.phyregs.state_datain);
}

void ide_phy_start_write(uint32_t blocklen, int udma_mode)
{
    if (blocklen & 1) blocklen++;

    g_idecomm.udma_mode = udma_mode;
    g_idecomm.datablocksize = blocklen;
    g_idecomm.udma_checksum_errors = 0;
    g_ide_phy.transfer_running = true;

    // Actual write is started when first block is written
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
        // In PIO mode the data is packed into 32 bits per each 16 bit word
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

    assert(blocklen == g_idecomm.datablocksize);
    assert(sio_hw->fifo_st & SIO_FIFO_ST_RDY_BITS);

    // Copy data to a block that remains valid for duration of transfer
    uint8_t *block = get_block_pointer();

    if (g_idecomm.udma_mode < 0)
    {
        // PIO data requires special formatting
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

    ide_phy_post_request(CORE1_REQ_START_DATAIN);
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

static void data_out_give_next_block()
{
    // Select a buffer for the transfer
    uint8_t *block = get_block_pointer();

    // Tell core1 to receive the block
    assert(sio_hw->fifo_st & SIO_FIFO_ST_RDY_BITS);
    sio_hw->fifo_wr = (uint32_t)block;

    ide_phy_post_request(CORE1_REQ_START_DATAOUT);
    g_ide_phy.transfer_block_start_time = millis();
}

void ide_phy_start_read(uint32_t blocklen, int udma_mode)
{
    if (blocklen & 1) blocklen++;

    g_idecomm.udma_mode = udma_mode;
    g_idecomm.datablocksize = blocklen;
    g_idecomm.udma_checksum_errors = 0;
    g_ide_phy.transfer_running = true;

    data_out_give_next_block();
}

void ide_phy_start_ata_read(uint32_t blocklen, int udma_mode)
{
    ide_phy_start_read(blocklen, udma_mode);
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
        data_out_give_next_block();
    }

    if (g_idecomm.udma_mode < 0)
    {
        // TODO: unroll
        // Narrowing conversion from 32 bits
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

    if (!continue_transfer)
    {
        // Transfer has ended, assert the interrupt.
        // TODO: Why is this done here instead of in ide_rigid.cpp?
        ide_phy_assert_irq(IDE_STATUS_DEVRDY | IDE_STATUS_DSC);
    }
}

void ide_phy_stop_transfers(int *crc_errors)
{
    ide_phy_post_request(CORE1_REQ_STOP_TRANSFERS);
    ide_phy_clear_event(CORE1_EVT_DATA_DONE);
    g_idecomm.datablocksize = 0;
    g_ide_phy.transfer_running = false;

    // Drain FIFO
    while (sio_hw->fifo_st & SIO_FIFO_ST_VLD_BITS)
    {
        (void)sio_hw->fifo_rd;
    }

    if (crc_errors) *crc_errors = g_idecomm.udma_checksum_errors;
}

void ide_phy_assert_irq(uint8_t ide_status)
{
    g_idecomm.phyregs.regs.status = ide_status;
    ide_phy_post_request(CORE1_REQ_ASSERT_IRQ);
    // dbgmsg("ASSERT_IRQ, status ", ide_status);
}

void ide_phy_set_signals(uint8_t signals)
{
    /* FIXME: This might be responsible for occassionally disturbing bus communication
    g_idecomm.set_signals = signals;
    ide_phy_post_request(CORE1_REQ_SET_SIGNALS);
    */
}

uint8_t ide_phy_get_signals()
{
    return IDE_SIGNAL_DASP | IDE_SIGNAL_PDIAG;

    /* FIXME: This occassionally disturbs bus communications.
    static uint32_t last_poll = 0;

    // The DASP and PDIAG signals are held for several seconds
    // so we don't need to poll them that often.
    uint32_t time_now = millis();
    if ((uint32_t)(time_now - last_poll) > 50)
    {
        last_poll = time_now;
        ide_phy_post_request(CORE1_REQ_GET_SIGNALS);
    }

    return g_idecomm.get_signals;*/
}

const ide_phy_capabilities_t *ide_phy_get_capabilities()
{
    return &g_ide_phy_capabilities;
}
