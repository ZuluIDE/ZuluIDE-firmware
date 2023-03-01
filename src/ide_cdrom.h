// Implements IDE command handlers for emulating an ATAPI CD-ROM drive.

#pragma once

#include "ide_protocol.h"

class IDECDROMDevice: public IDEDevice
{
public:
    IDECDROMDevice();

    virtual void poll();

    virtual bool handle_command(ide_phy_msg_t *msg);

    virtual void handle_event(ide_phy_msg_t *msg);

protected:
    uint16_t m_bytes_req; // Host requested bytes per transfer
    uint8_t m_sense_key; // Latest error info
    uint16_t m_sense_asc;

    bool cmd_identify_packet_device(ide_phy_msg_t *msg);
    bool cmd_packet(ide_phy_msg_t *msg);
    bool set_packet_device_signature(uint8_t error);
    bool set_atapi_byte_count(uint16_t byte_count);
    bool atapi_send_data(const uint16_t *data, uint16_t byte_count);

    bool handle_atapi_command(const uint8_t *cmd);
    bool atapi_cmd_error(uint8_t sense_key, uint16_t sense_asc);
    bool atapi_cmd_ok();

    bool atapi_test_unit_ready(const uint8_t *cmd);
    bool atapi_inquiry(const uint8_t *cmd);
};
