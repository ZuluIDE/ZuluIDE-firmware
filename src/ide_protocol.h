// High-level implementation of IDE command handling
// Refer to https://pdos.csail.mit.edu/6.828/2005/readings/hardware/ATA-d1410r3a.pdf

#pragma once

#include "ide_constants.h"
#include "ide_phy.h"

// This interface is used for implementing emulated IDE devices
class IDEDevice
{
public:
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
};

// Initialize the protocol layer with devices
void ide_protocol_init(IDEDevice *primary, IDEDevice *secondary);

// Call this periodically to process events
void ide_protocol_poll();
