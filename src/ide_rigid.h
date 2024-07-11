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
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
**/

// Implements IDE command handlers for generic ATAPI device.

#pragma once

#include "ide_protocol.h"
#include "ide_imagefile.h"
#include <stddef.h>

// Number of simultaneous transfer requests to pass to ide_phy.
#define ATAPI_TRANSFER_REQ_COUNT 2

// Generic PATA rigid device implementation:)
class IDERigidDevice: public IDEDevice, public IDEImage::Callback
{
public:
    virtual void initialize(int devidx) override;

    virtual void reset() override;

    virtual void set_image(IDEImage *image);

    virtual bool handle_command(ide_registers_t *regs);

    virtual void handle_event(ide_event_t event);

    virtual bool disables_iordy() override { return true; }

    virtual bool is_packet_device() { return false; }

    virtual bool is_medium_present() {return has_image();}

    virtual bool has_image() { return m_image != nullptr; }

    virtual uint64_t capacity() { return (m_image ? m_image->capacity() : 0); }

    virtual uint64_t capacity_lba() { return capacity() / m_devinfo.bytes_per_sector; }

    virtual bool set_device_signature(uint8_t error, bool was_reset) override;

    virtual void fill_device_signature(ide_registers_t *regs) override;

    virtual void eject_button_poll(bool immediate) {;}

    virtual void sd_card_inserted() {;}

protected:
    IDEImage *m_image;

    // Device type info is filled in by subclass
    struct {
        uint8_t devtype;
        bool removable;
        bool writable;
        uint32_t bytes_per_sector;
        // uint8_t media_status_events;
        uint8_t sectors_per_track;
        uint8_t heads;
        char serial_number[20+1];
        char firmware_rev[8+1];
        char model_number[40+1];

    } m_devinfo;

    enum ata_data_state_t {
        ATA_DATA_IDLE,
        ATA_DATA_WRITE,
        ATA_DATA_READ
    };

    // ATA command state
    struct {
        uint16_t blocksize; // Block size for data transfers
        ata_data_state_t data_state;
        int udma_mode;  // Negotiated udma mode, or negative if not enabled
        bool dma_requested; // Host requests to use DMA transfer for current command
        int crc_errors; // CRC errors in latest transfer
    } m_ata_state;

    struct
    {
        bool ejected;
        bool reinsert_media_on_inquiry;
        bool reinsert_media_after_eject;
    } m_removable;

    // Buffer used for responses, ide_phy code benefits from this being aligned to 32 bits
    // Enough for any inquiry/mode response and for up to one CD sector.
    union {
        uint32_t dword[588];
        uint16_t word[1176];
        uint8_t bytes[2352];
    } m_buffer;

    // IDE command handlers
    virtual bool cmd_nop(ide_registers_t *regs);
    virtual bool cmd_set_features(ide_registers_t *regs);
    virtual bool cmd_read(ide_registers_t *regs, bool dma_transfer);
    virtual bool cmd_write(ide_registers_t *regs, bool dma_transfer);
    virtual bool cmd_init_dev_params(ide_registers_t *regs);
    virtual bool cmd_identify_device(ide_registers_t *regs);
    virtual bool cmd_recalibrate(ide_registers_t *regs);

    // Helper methods
    // convert lba to cylinder, head, sector values
    void lba2chs(const uint32_t lba, uint16_t &cylinder, uint8_t &head, uint8_t &sector);
    // Methods used by ATA command implementations
    // send data
    ssize_t ata_send_data(const uint8_t *data, size_t blocksize, size_t num_blocks);
    // Send single data block. Waits for space in buffer, but doesn't wait for new transfer to finish.
    bool ata_send_data_block(const uint8_t *data, uint16_t blocksize);
    // Wait for any previously started transfers to finish
    bool ata_send_wait_finish();
    // Receive one or multiple data blocks synchronously
    bool ata_recv_data(uint8_t *data, size_t blocksize, size_t num_blocks = 1);
    // Receive single data block
    bool ata_recv_data_block(uint8_t *data, uint16_t blocksize);

    // Methods used by ATAPI command implementations

    // Send one or multiple data blocks synchronously and wait for transfer to finish
    bool ata_send_chunked_data(const uint8_t *data, size_t blocksize, size_t num_blocks = 1);

    // Report error or successful completion of ATAPI command
    bool atapi_cmd_error(uint8_t sense_key, uint16_t sense_asc);
    bool atapi_cmd_ok();

    // ATAPI command handlers

    // Read handlers
    virtual ssize_t read_callback(const uint8_t *data, size_t blocksize, size_t num_blocks);

    // Write handlers
    virtual ssize_t write_callback(uint8_t *data, size_t blocksize, size_t num_blocks);
};


