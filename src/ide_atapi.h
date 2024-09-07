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

#define ATAPI_AUDIO_CD_SECTOR_SIZE 2352

// Number of simultaneous transfer requests to pass to ide_phy.
#define ATAPI_TRANSFER_REQ_COUNT 2

// \todo - find a better way to link to the static backing data
extern IDEImageFile g_ide_imagefile;

// Generic ATAPI device implementation: encapsulated SCSI commands over ATA.
// Abstract class, use one of the subclasses (IDECDROMDevice)
class IDEATAPIDevice: public IDEDevice, public IDEImage::Callback
{
public:
    virtual void initialize(int devidx) override;

    virtual void reset() override;

    virtual void set_image(IDEImage *image);

    virtual bool handle_command(ide_registers_t *regs);

    virtual void handle_event(ide_event_t event);

    virtual bool is_packet_device() { return true; }

    virtual bool is_medium_present() override;

    virtual bool has_image() { return m_image != nullptr; }

    virtual uint64_t capacity() { return (m_image ? m_image->capacity() : 0); }

    virtual uint64_t capacity_lba() { return capacity() / m_devinfo.bytes_per_sector; }

    virtual void eject_button_poll(bool immediate);

    virtual void button_eject_media();

    virtual void eject_media();

    virtual void insert_media(IDEImage *image = nullptr);

    virtual void sd_card_inserted() override;

    virtual bool set_device_signature(uint8_t error, bool was_reset) override;

    virtual void fill_device_signature(ide_registers_t *regs) override;



protected:
    IDEImage *m_image;

    // Device type info is filled in by subclass
    struct {
        uint8_t devtype;
        bool removable;
        bool writable;
        uint32_t bytes_per_sector;
        uint8_t media_status_events;

        // Response to INQUIRY
        char atapi_vendor[8];
        char atapi_product[16];
        char atapi_version[4];

        // Profiles reported to GET CONFIGURATION
        uint16_t num_profiles;
        uint16_t profiles[8];
        uint16_t current_profile;

        // Medium type reported by MODE SENSE
        uint8_t medium_type;
    } m_devinfo;

    enum atapi_data_state_t {
        ATAPI_DATA_IDLE,
        ATAPI_DATA_WRITE,
        ATAPI_DATA_READ
    };

    // ATAPI command state
    struct {
        uint16_t bytes_req; // Host requested bytes per transfer
        uint8_t sense_key; // Latest error class
        uint16_t sense_asc; // Latest error details
        uint16_t blocksize; // Block size for data transfers
        atapi_data_state_t data_state;
        int udma_mode;  // Negotiated udma mode, or negative if not enabled
        bool dma_requested; // Host requests to use DMA transfer for current command
        bool unit_attention;
        bool not_ready;
        int crc_errors; // CRC errors in latest transfer
    } m_atapi_state;

    struct
    {
        bool ejected;
        bool reinsert_media_on_inquiry;
        bool reinsert_media_after_eject;
        bool reinsert_media_after_sd_insert;
        bool prevent_removable;
        bool prevent_persistent;
        bool ignore_prevent_removal;
    } m_removable;

    // Buffer used for responses, ide_phy code benefits from this being aligned to 32 bits
    // Enough for any inquiry/mode response and for up to one CD sector.
    union {
        uint32_t dword[588];
        uint16_t word[1176];
        uint8_t bytes[ATAPI_AUDIO_CD_SECTOR_SIZE];
    } m_buffer;

    // IDE command handlers
    virtual bool cmd_nop(ide_registers_t *regs);
    virtual bool cmd_set_features(ide_registers_t *regs);
    virtual bool cmd_identify_packet_device(ide_registers_t *regs);
    virtual bool cmd_packet(ide_registers_t *regs);
    virtual bool cmd_device_reset(ide_registers_t *regs);

    // Methods used by ATAPI command implementations

    // Send one or multiple data blocks synchronously and wait for transfer to finish
    bool atapi_send_data(const uint8_t *data, size_t blocksize, size_t num_blocks = 1);

    // Send one or multiple data block asynchronously.
    // Returns number of blocks written to buffer, or negative on error.
    ssize_t atapi_send_data_async(const uint8_t *data, size_t blocksize, size_t num_blocks = 1);

    // Query whether calling atapi_send_data_block() would proceed immediately.
    bool atapi_send_data_is_ready(size_t blocksize);

    // Send single data block. Waits for space in buffer, but doesn't wait for new transfer to finish.
    bool atapi_send_data_block(const uint8_t *data, uint16_t blocksize);

    // Wait for any previously started transfers to finish
    bool atapi_send_wait_finish();

    // Receive one or multiple data blocks synchronously
    bool atapi_recv_data(uint8_t *data, size_t blocksize, size_t num_blocks = 1);

    // Receive single data block
    bool atapi_recv_data_block(uint8_t *data, uint16_t blocksize);

    // Report error or successful completion of ATAPI command
    bool atapi_cmd_error(uint8_t sense_key, uint16_t sense_asc);
    virtual bool atapi_cmd_not_ready_error();
    bool atapi_cmd_ok();

    // ATAPI command handlers
    virtual bool handle_atapi_command(const uint8_t *cmd);
    virtual bool atapi_test_unit_ready(const uint8_t *cmd);
    virtual bool atapi_start_stop_unit(const uint8_t *cmd);
    virtual bool atapi_prevent_allow_removal(const uint8_t *cmd);
    virtual bool atapi_inquiry(const uint8_t *cmd);
    virtual bool atapi_mode_sense(const uint8_t *cmd);
    virtual bool atapi_mode_select(const uint8_t *cmd);
    virtual bool atapi_request_sense(const uint8_t *cmd);
    virtual bool atapi_get_configuration(const uint8_t *cmd);
    virtual bool atapi_get_event_status_notification(const uint8_t *cmd);
    virtual bool atapi_read_capacity(const uint8_t *cmd);
    virtual bool atapi_read(const uint8_t *cmd);
    virtual bool atapi_write(const uint8_t *cmd);

    // Read handlers
    virtual bool doRead(uint32_t lba, uint32_t transfer_len);
    virtual ssize_t read_callback(const uint8_t *data, size_t blocksize, size_t num_blocks);

    // Write handlers
    virtual bool doWrite(uint32_t lba, uint32_t transfer_len);
    virtual ssize_t write_callback(uint8_t *data, size_t blocksize, size_t num_blocks, bool first_xfer, bool last_xfer);

    // ATAPI mode pages
    virtual size_t atapi_get_mode_page(uint8_t page_ctrl, uint8_t page_idx, uint8_t *buffer, size_t max_bytes);
    virtual void atapi_set_mode_page(uint8_t page_ctrl, uint8_t page_idx, const uint8_t *buffer, size_t length);

    // ATAPI get_configuration responses
    virtual size_t atapi_get_configuration(uint8_t return_type, uint16_t feature, uint8_t *buffer, size_t max_bytes);

    // ATAPI standard inquiry string settings
    void set_inquiry_strings(const char* default_vendor, const char* default_product, const char* default_version);
};
