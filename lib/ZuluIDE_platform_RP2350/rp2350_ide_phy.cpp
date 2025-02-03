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

#include <zuluide_rp2350b_core1.h>
#ifndef ZULUIDE_RP2350B_CORE1_HAVE_SOURCE
#error Binary blob loading not yet implemented
#endif

static struct {
    ide_phy_config_t config;
    bool transfer_running;
    int udma_mode;
    volatile bool watchdog_error;

    int crc_errors;
    uint32_t block_crc0;
    uint32_t block_crc1;

    uint32_t bufferidx; // Index of next buffer in g_idebuffers to use
} g_ide_phy;

static ide_phy_capabilities_t g_ide_phy_capabilities = {
    .max_blocksize = IDECOMM_MAX_BLOCKSIZE,

    .supports_iordy = true,
    .max_pio_mode = 0,
    .min_pio_cycletime_no_iordy = 240,
    .min_pio_cycletime_with_iordy = 180,

    .max_udma_mode = -1,
};

void ide_phy_reset(const ide_phy_config_t* config)
{
    // Only initialize registers once after boot, after that ide_protocol handles it.
    static bool regs_inited = false;
    phy_ide_registers_t phyregs = g_idecomm.phyregs;
    if (!regs_inited)
    {
        memset(&phyregs, 0, sizeof(phyregs));
        regs_inited = true;
    }
    __sync_synchronize();

    g_ide_phy.config = *config;
    g_ide_phy.watchdog_error = false;

    dbgmsg("ide_phy_reset");
    zuluide_rp2350b_core1_run();

    g_idecomm.enable_dev0          = config->enable_dev0;
    g_idecomm.enable_dev1          = config->enable_dev1;
    g_idecomm.enable_dev1_zeros    = config->enable_dev1_zeros;
    g_idecomm.atapi_dev0           = config->atapi_dev0;
    g_idecomm.atapi_dev1           = config->atapi_dev1;
    g_idecomm.disable_iordy        = config->disable_iordy;
    g_idecomm.enable_packet_intrq  = config->enable_packet_intrq;
    phyregs.state_irqreq = 0;
    phyregs.state_datain = 0;
    phyregs.state_dataout = 0;
    g_idecomm.phyregs = phyregs;
    sio_hw->doorbell_out_set = EVT_IN_SET_REGS;
}

void ide_phy_reset_from_watchdog()
{
    g_ide_phy.watchdog_error = true;
}

ide_event_t ide_phy_get_events()
{
    uint8_t doorbell = sio_hw->doorbell_in_clr;

    if (doorbell & EVT_OUT_CMD_RECEIVED)
    {
        sio_hw->doorbell_in_clr = EVT_OUT_CMD_RECEIVED;
        dbgmsg("IDE_EVENT_CMD, status ", g_idecomm.phyregs.regs.status);
        return IDE_EVENT_CMD;
    }
    else if (g_idecomm.hwrst_flag)
    {
        g_idecomm.hwrst_flag = 0;
        delay(1);
        if (g_idecomm.hwrst_flag)
        {
            // Reset still continues, report when it ends
            return IDE_EVENT_NONE;
        }
        else
        {
            return IDE_EVENT_HWRST;
        }
    }
    else if (g_idecomm.swrst_flag)
    {
        if (g_idecomm.phyregs.regs.device_control & IDE_DEVCTRL_SRST)
        {
            // Software reset continues
            return IDE_EVENT_NONE;
        }
        else
        {
            // Software reset done
            g_idecomm.swrst_flag = 0;
            return IDE_EVENT_SWRST;
        }
    }

    return IDE_EVENT_NONE;
}

bool ide_phy_is_command_interrupted()
{
    return false;
}

void ide_phy_get_regs(ide_registers_t *regs)
{
    *regs = g_idecomm.phyregs.regs;
    // dbgmsg("GET_REGS, status ", regs->status, " error ", regs->error, " lba_high ", regs->lba_high);
}

void ide_phy_set_regs(const ide_registers_t *regs)
{
    phy_ide_registers_t phyregs = g_idecomm.phyregs;
    phyregs.regs = *regs;
    g_idecomm.phyregs = phyregs;
    __sync_synchronize();
    sio_hw->doorbell_out_set = EVT_IN_SET_REGS;
    dbgmsg("SET_REGS, status ", regs->status, " error ", regs->error, " lba_high ", regs->lba_high, " data_in ", (int)phyregs.state_datain);

    delay(2);
    phyregs = g_idecomm.phyregs;
    dbgmsg("READBACK status ", phyregs.regs.status, " error ", phyregs.regs.error, " lba_high ", phyregs.regs.lba_high, " data_in ", (int)phyregs.state_datain);
}

void ide_phy_start_write(uint32_t blocklen, int udma_mode)
{
    assert(udma_mode < 0); // TODO: Implement UDMA
    g_idecomm.datablocksize = blocklen;

    // Actual write is started when first block is written
}

bool ide_phy_can_write_block()
{
    return (sio_hw->fifo_st & SIO_FIFO_ST_RDY_BITS);
}

void ide_phy_write_block(const uint8_t *buf, uint32_t blocklen)
{
    assert(blocklen == g_idecomm.datablocksize);
    assert(sio_hw->fifo_st & SIO_FIFO_ST_RDY_BITS);

    // Copy data to a block that remains valid for duration of transfer
    // Data must be aligned so that it ends at the end of the block.
    uint8_t *block = g_idebuffers[g_ide_phy.bufferidx++ % IDECOMM_BUFFERCOUNT];
    block += IDECOMM_MAX_BLOCKSIZE - blocklen;
    memcpy(block, buf, blocklen);

    dbgmsg("Write block ptr ", (uint32_t)block, " length ", (int)blocklen);

    // Give the transmit pointer to core 1
    sio_hw->fifo_wr = (uint32_t)block;

    if (!g_idecomm.phyregs.state_datain)
    {
        // Wake up core 1
        // FIXME: Possible race condition if core1 unsets state_datain after we check it
        phy_ide_registers_t phyregs = g_idecomm.phyregs;
        phyregs.state_datain = 1;
        g_idecomm.phyregs = phyregs;
        sio_hw->doorbell_out_set = EVT_IN_SET_REGS;
    }
}

bool ide_phy_is_write_finished()
{
    return !g_idecomm.phyregs.state_datain;
}

static void data_out_give_next_block()
{
    // Select a buffer for the transfer
    // Data must be aligned so that it ends at the end of the block.
    uint8_t *block = g_idebuffers[g_ide_phy.bufferidx++ % IDECOMM_BUFFERCOUNT];
    block += IDECOMM_MAX_BLOCKSIZE - g_idecomm.datablocksize;

    // Tell core1 to receive the block
    assert(sio_hw->fifo_st & SIO_FIFO_ST_RDY_BITS);
    sio_hw->fifo_wr = (uint32_t)block;

    if (!g_idecomm.phyregs.state_dataout)
    {
        // Wake core1 up
        // FIXME: Possible race condition if core1 unsets state_dataout after we check it
        phy_ide_registers_t phyregs = g_idecomm.phyregs;
        phyregs.state_dataout = 1;
        g_idecomm.phyregs = phyregs;
        sio_hw->doorbell_out_set = EVT_IN_SET_REGS;
    }
}

void ide_phy_start_read(uint32_t blocklen, int udma_mode)
{
    assert(udma_mode < 0); // TODO: Implement UDMA
    g_idecomm.datablocksize = blocklen;

    data_out_give_next_block();
}

void ide_phy_start_ata_read(uint32_t blocklen, int udma_mode)
{
    assert(false); // TODO: implement
}

bool ide_phy_can_read_block()
{
    return sio_hw->fifo_st & SIO_FIFO_ST_VLD_BITS;
}

void ide_phy_start_read_buffer(uint32_t blocklen)
{
    assert(false); // TODO: implement
}

void ide_phy_read_block(uint8_t *buf, uint32_t blocklen, bool continue_transfer)
{
    assert(blocklen == g_idecomm.datablocksize);
    assert(sio_hw->fifo_st & SIO_FIFO_ST_VLD_BITS);
    const uint8_t *buf2 = (const uint8_t*)sio_hw->fifo_rd;

    memcpy(buf, buf2, blocklen);

    if (continue_transfer)
    {
        data_out_give_next_block();
    }
}

void ide_phy_ata_read_block(uint8_t *buf, uint32_t blocklen, bool continue_transfer)
{
}

void ide_phy_stop_transfers(int *crc_errors)
{
    phy_ide_registers_t phyregs = g_idecomm.phyregs;
    phyregs.state_datain = 0;
    phyregs.state_dataout = 0;
    g_idecomm.phyregs = phyregs;
    sio_hw->doorbell_out_set = EVT_IN_SET_REGS;
    g_idecomm.datablocksize = 0;

    // Drain FIFO
    while (sio_hw->fifo_st & SIO_FIFO_ST_VLD_BITS)
    {
        (void)sio_hw->fifo_rd;
    }
}

void ide_phy_assert_irq(uint8_t ide_status)
{
    phy_ide_registers_t phyregs = g_idecomm.phyregs;
    phyregs.regs.status = ide_status;
    phyregs.state_irqreq = 1;
    g_idecomm.phyregs = phyregs;
    sio_hw->doorbell_out_set = EVT_IN_SET_REGS;
    dbgmsg("ASSERT_IRQ, status ", phyregs.regs.status);
}

void ide_phy_set_signals(uint8_t signals)
{
}

uint8_t ide_phy_get_signals()
{
    return 0;
}

const ide_phy_capabilities_t *ide_phy_get_capabilities()
{
    return &g_ide_phy_capabilities;
}
