// High-level implementation of IDE command handling
// Refer to https://pdos.csail.mit.edu/6.828/2005/readings/hardware/ATA-d1410r3a.pdf

#pragma once

#include "ide_constants.h"
#include "ide_phy.h"

void ide_protocol_init();
void ide_protocol_poll();

// This interface is used for implementing emulated IDE devices
class IDEDevice
{
public:
    // Called periodically by main loop
    virtual void poll() = 0;

    // Called whenever new command is received from host.
    // The handler should use ide_phy_send_msg() to send response.
    // Returning false results in IDE_ERROR_ABORT response.
    virtual bool handle_command(ide_phy_msg_t *msg) = 0;

    // Called for other PHY events than new commands
    // Implementation can be empty.
    virtual void handle_event(ide_phy_msg_t *msg) = 0;
};

