/**
 * ZuluIDE™ - Copyright (c) 2023 Rabbit Hole Computing™
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

// Platform-independent API for IDE physical layer access.

#pragma once

#include <stdint.h>

enum ide_event_t {
    IDE_EVENT_NONE = 0,
    IDE_EVENT_HWRST,
    IDE_EVENT_SWRST,
    IDE_EVENT_CMD,
    IDE_EVENT_DATA_TRANSFER_DONE,
    IDE_EVENT_CMD_EXE_DEV_DIAG
};

struct ide_registers_t {
    uint8_t status;
    uint8_t command;
    uint8_t device;
    uint8_t device_control;
    uint8_t error;
    uint8_t feature;
    uint8_t sector_count;
    uint8_t lba_low;
    uint8_t lba_mid;
    uint8_t lba_high;
};

struct ide_phy_config_t {
    bool enable_dev0; // Answer to register reads for device 0 with actual data
    bool enable_dev1; // Answer to register reads for device 1 with actual data
    bool enable_dev1_zeros; // Answer to register reads for device 1 with zeros
    bool atapi_dev0; // Automatically read command for ATAPI PACKET on device 0
    bool atapi_dev1; // Automatically read command for ATAPI PACKET on device 1
    bool disable_iordy; // Disable IORDY in PIO mode 
    // Enables INTRQ between the initial ATA PACKET command and receiving the ATAPI command
    bool enable_packet_intrq;
};

// Reset the IDE phy
void ide_phy_reset(const ide_phy_config_t* config);

// Print debug information to log, called when something goes wrong
void ide_phy_print_debug();

// Poll for new events.
// Returns IDE_EVENT_NONE if no new events.
ide_event_t ide_phy_get_events();

// Check if there is an event that should interrupt current command execution
bool ide_phy_is_command_interrupted();

// Get current state of IDE registers
void ide_phy_get_regs(ide_registers_t *regs);

// Set current state of IDE registers
void ide_phy_set_regs(const ide_registers_t *regs);

// IDE data transfer happens in DRQ blocks, the size of which can be negotiated
// between host and device.
//
// Usage for transfers to IDE bus:
// 1. Device implementation calls ide_phy_start_write() to set block size.
// 2. Device implementation calls ide_phy_write_block() to write data payload.
//    PHY sets status to DEVRDY | DATAREQ and asserts interrupt.
// 3. Host reads IDE data register to transfer data.
// 4. At the end of the block, PHY sets status to BSY.
//    Device implementation can wait for IDE_EVENT_DATA_TRANSFER_DONE or poll ide_phy_is_write_finished().
//    More data can be written as soon as ide_phy_can_write_block() returns true.
//
// Usage for transfers from IDE bus:
// 1. Device implementation calls ide_phy_start_read() to set block size.
//    PHY sets status to DEVRDY | DATAREQ and asserts interrupt.
// 2. Host writes IDE data register to transfer data.
// 3. At the end of the block, PHY sets status to BSY.
//    Device implementation can wait for IDE_EVENT_DATA_TRANSFER_DONE or poll ide_phy_can_read_block().
// 4. Device implementation calls ide_phy_read_block().
//
// Calling ide_phy_stop_transfers() stops any previously started transfer.
//
// If udma_mode is 0 or larger, the PHY may use UDMA transfers if they are supported.

void ide_phy_start_write(uint32_t blocklen, int udma_mode = -1);
bool ide_phy_can_write_block();
void ide_phy_write_block(const uint8_t *buf, uint32_t blocklen);
bool ide_phy_is_write_finished();

void ide_phy_start_read(uint32_t blocklen, int udma_mode = -1);
void ide_phy_start_ata_read(uint32_t blocklen, int udma_mode = -1);
bool ide_phy_can_read_block();
void ide_phy_start_read_buffer(uint32_t blocklen);
void ide_phy_read_block(uint8_t *buf, uint32_t blocklen, bool continue_transfer = false);
void ide_phy_ata_read_block(uint8_t *buf, uint32_t blocklen, bool continue_transfer = false);

// Stop any running or finished transfers.
// In UltraDMA mode crc_errors receives the number of detected CRC errors since start_write/start_read
void ide_phy_stop_transfers(int *crc_errors = nullptr);

// Assert IDE interrupt and set status register
void ide_phy_assert_irq(uint8_t ide_status);

// Set IDE phy diagnostics signals
// Bit set = drive signal to 0 state, bit clear = high-impedance
#define IDE_SIGNAL_DASP     0x01
#define IDE_SIGNAL_PDIAG    0x02
void ide_phy_set_signals(uint8_t signals);
uint8_t ide_phy_get_signals();

// Query what is supported by the IDE PHY
struct ide_phy_capabilities_t
{
    uint32_t max_blocksize;
    bool supports_iordy;
    int max_pio_mode;
    int min_pio_cycletime_no_iordy;
    int min_pio_cycletime_with_iordy;
    int max_udma_mode; // -1 if UDMA not supported
};

const ide_phy_capabilities_t *ide_phy_get_capabilities();
