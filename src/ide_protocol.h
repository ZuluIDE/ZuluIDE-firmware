// High-level implementation of IDE command handling
// Refer to https://pdos.csail.mit.edu/6.828/2005/readings/hardware/ATA-d1410r3a.pdf

#pragma once

#include "ide_constants.h"
#include "ide_phy.h"

// This interface is used for implementing emulated IDE devices
class IDEDevice
{
public:
    // Loads configuration
    virtual void initialize(int devidx);

    // Called periodically by main loop
    virtual void poll() = 0;

    // Called whenever new command is received from host.
    // The handler should use ide_phy_send_msg() to send response.
    // Returning false results in IDE_ERROR_ABORT response.
    virtual bool handle_command(ide_registers_t *regs) = 0;

    // Called for other PHY events than new commands
    // Implementation can be empty.
    virtual void handle_event(ide_event_t event) = 0;

    // Returns true if this device implements the ATAPI packet command set 
    virtual bool is_packet_device() { return false; }

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
