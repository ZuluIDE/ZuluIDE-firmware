// Platform-independent API for IDE physical layer access.


#pragma once

#include <stdint.h>

enum ide_event_t {
    IDE_EVENT_NONE = 0,
    IDE_EVENT_HWRST,
    IDE_EVENT_SWRST,
    IDE_EVENT_CMD,
    IDE_EVENT_DATA_TRANSFER_DONE
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
};

// Reset the IDE phy
void ide_phy_reset(const ide_phy_config_t* config);

// Poll for new events.
// Returns IDE_EVENT_NONE if no new events.
ide_event_t ide_phy_get_events();

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

void ide_phy_start_write(uint32_t blocklen);
bool ide_phy_can_write_block();
void ide_phy_write_block(const uint8_t *buf, uint32_t blocklen);
bool ide_phy_is_write_finished();

void ide_phy_start_read(uint32_t blocklen);
bool ide_phy_can_read_block();
void ide_phy_read_block(uint8_t *buf, uint32_t blocklen);

void ide_phy_stop_transfers();

// Assert IDE interrupt and set status register
void ide_phy_assert_irq(uint8_t ide_status);

// Query what is supported by the IDE PHY
struct ide_phy_capabilities_t
{
    uint32_t max_blocksize;
    bool supports_iordy;
    int max_pio_mode;
    int min_pio_cycletime_no_iordy;
    int min_pio_cycletime_with_iordy;
};

const ide_phy_capabilities_t *ide_phy_get_capabilities();
