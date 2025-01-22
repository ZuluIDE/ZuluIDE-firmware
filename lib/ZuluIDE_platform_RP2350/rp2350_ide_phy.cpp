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
} g_ide_phy;

static ide_phy_capabilities_t g_ide_phy_capabilities = {
    .max_blocksize = 4096,

    .supports_iordy = true,
    .max_pio_mode = 0,
    .min_pio_cycletime_no_iordy = 240,
    .min_pio_cycletime_with_iordy = 180,

    .max_udma_mode = -1,
};

void ide_phy_reset(const ide_phy_config_t* config)
{
    g_ide_phy.config = *config;
    g_ide_phy.watchdog_error = false;
}

void ide_phy_reset_from_watchdog()
{
    g_ide_phy.watchdog_error = true;
}

ide_event_t ide_phy_get_events()
{
    return IDE_EVENT_NONE;
}

bool ide_phy_is_command_interrupted()
{
    return false;
}

void ide_phy_get_regs(ide_registers_t *regs)
{
}

void ide_phy_set_regs(const ide_registers_t *regs)
{
}

void ide_phy_start_write(uint32_t blocklen, int udma_mode)
{
}

bool ide_phy_can_write_block()
{
    return true;
}

void ide_phy_write_block(const uint8_t *buf, uint32_t blocklen)
{
}

bool ide_phy_is_write_finished()
{
    return true;
}

void ide_phy_start_read(uint32_t blocklen, int udma_mode)
{
}

void ide_phy_start_ata_read(uint32_t blocklen, int udma_mode)
{
}

bool ide_phy_can_read_block()
{
    return true;
}

void ide_phy_start_read_buffer(uint32_t blocklen)
{
}

void ide_phy_read_block(uint8_t *buf, uint32_t blocklen, bool continue_transfer)
{
}

void ide_phy_ata_read_block(uint8_t *buf, uint32_t blocklen, bool continue_transfer)
{
}

void ide_phy_stop_transfers(int *crc_errors)
{
}

void ide_phy_assert_irq(uint8_t ide_status)
{
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
