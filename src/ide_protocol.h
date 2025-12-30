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

// High-level implementation of IDE command handling
// Refer to https://pdos.csail.mit.edu/6.828/2005/readings/hardware/ATA-d1410r3a.pdf

#pragma once

#include "ide_constants.h"
#include "ide_phy.h"
#include "ide_imagefile.h"

// This interface is used for implementing emulated IDE devices
class IDEDevice
{
public:
    // Loads configuration
    virtual void initialize(int devidx);

    // Finishes initialization after the first image has been loaded
    virtual void post_image_setup() = 0;

    // Resets member variables after IDE/ATA reset
    virtual void reset() = 0;

    // Set image backing file to access
    virtual void set_image(IDEImage *image) = 0;

    // polls eject buttons status
    virtual void eject_button_poll(bool immediate) = 0;

    virtual void eject_media() {;}

    // device medium is present
    virtual bool is_medium_present() = 0;

    // tests if an image is open
    virtual bool has_image() = 0;

    // Called whenever new command is received from host.
    // The handler should use ide_phy_send_msg() to send response.
    // Returning false results in IDE_ERROR_ABORT response.
    virtual bool handle_command(ide_registers_t *regs) = 0;

    // Called for other PHY events than new commands
    // Implementation can be empty.
    virtual void handle_event(ide_event_t event) = 0;

    // Returns true if this device implements the ATAPI packet command set
    virtual bool is_packet_device() { return false; }

    // Returns true if this device does not use the IORdy signal
    virtual bool disables_iordy() { return false; }

    virtual bool set_device_signature(uint8_t error, bool was_reset) = 0;

    // Set signature values for ide register
    virtual void fill_device_signature(ide_registers_t *regs) = 0;

    // Called when an SD card is reinserted
    virtual void sd_card_inserted() = 0;

    // For removable media devices - insert current
    virtual void insert_media(IDEImage *image = nullptr) = 0;

    // For removable media devices - insert next
    virtual void insert_next_media(IDEImage *image = nullptr) = 0;

    // Deferred loading status
    virtual bool set_load_deferred(const char* image_name) = 0;
    virtual bool is_load_deferred() = 0;

    // The default value of the zuluide.ini setting atapi_intrq
    virtual bool atapi_intrq_default_on() {return false;}

    // Is device removable
    virtual bool is_removable() = 0;

    // Run when new media is loaded
    virtual void loaded_new_media() = 0;

    // This is the state of media for the device at init or SD insertion
    virtual bool is_loaded_without_media() = 0;
    virtual void set_loaded_without_media(bool no_media) = 0;
    virtual void set_load_first_image_cb(void (*load_image_cb)()) = 0;

protected:
    struct {
        int dev_index;
        int max_udma_mode;
        int max_pio_mode;
        int max_blocksize;
        // Response to IDENTIFY PACKET DEVICE/IDENTIFY DEVICE
        char ata_model[40];
        char ata_revision[8];
        char ata_serial[20];
        int ide_cylinders;
        int ide_heads;
        int ide_sectors;
        int access_delay;
    } m_devconfig;

    // PHY capabilities limited by active device configuration
    ide_phy_capabilities_t m_phy_caps;
    void formatDriveInfoField(char *field, int fieldsize, bool align_right);
    void set_ident_strings(const char* default_model, const char* default_serial, const char* default_revision);
};

// Initialize the protocol layer with devices
void ide_protocol_init(IDEDevice *primary, IDEDevice *secondary);

// Get phy config as information to device implementations
const ide_phy_config_t *ide_protocol_get_config();

// Call this periodically to process events
void ide_protocol_poll();

