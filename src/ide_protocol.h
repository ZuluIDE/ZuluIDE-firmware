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

    // Set image backing file to access
    virtual void set_image(IDEImage *image) = 0;

    // polls eject buttons status
    virtual void eject_button_poll(bool immediate) = 0;

    // device is ready
    virtual bool is_ready() = 0;

    virtual bool is_medium_present() = 0;

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

protected:
    struct {
        int dev_index;
        int max_udma_mode;
        int max_pio_mode;
        int max_blocksize;
    } m_devconfig;

    // PHY capabilities limited by active device configuration
    ide_phy_capabilities_t m_phy_caps;
};

// Initialize the protocol layer with devices
void ide_protocol_init(IDEDevice *primary, IDEDevice *secondary);

// Get phy config as information to device implementations
const ide_phy_config_t *ide_protocol_get_config();

// Call this periodically to process events
void ide_protocol_poll();
