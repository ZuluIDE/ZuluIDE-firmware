// Implements IDE command handlers for generic ATAPI device.

#pragma once

#include "ide_protocol.h"
#include <stddef.h>

class IDEATAPIDevice: public IDEDevice
{
public:
    IDEATAPIDevice();

    virtual void poll();

    virtual bool handle_command(ide_phy_msg_t *msg);

    virtual void handle_event(ide_phy_msg_t *msg);

protected:
    // Device type info is filled in by subclass
    struct {
        uint8_t devtype;
        bool removable;
        uint32_t max_lba;
        uint32_t bytes_per_sector;
    } m_devinfo;
    
    // ATAPI command state
    struct {
        uint16_t bytes_req; // Host requested bytes per transfer
        uint8_t sense_key; // Latest error class
        uint16_t sense_asc; // Latest error details
    } m_atapi_state;
    
    // Buffer used for responses, ide_phy code requires this to be aligned
    union {
        uint32_t dword[128];
        uint16_t word[256];
        uint8_t bytes[512];
    } m_buffer;
    
    // IDE command handlers
    virtual bool cmd_identify_packet_device(ide_phy_msg_t *msg);
    virtual bool cmd_packet(ide_phy_msg_t *msg);
    virtual bool set_packet_device_signature(uint8_t error);
    
    // Methods used by ATAPI command implementations
    bool set_atapi_byte_count(uint16_t byte_count);
    bool atapi_send_data(const uint16_t *data, uint16_t byte_count);
    bool atapi_cmd_error(uint8_t sense_key, uint16_t sense_asc);
    bool atapi_cmd_ok();

    // ATAPI command handlers
    virtual bool handle_atapi_command(const uint8_t *cmd);
    virtual bool atapi_test_unit_ready(const uint8_t *cmd);
    virtual bool atapi_inquiry(const uint8_t *cmd);
    virtual bool atapi_mode_sense(const uint8_t *cmd);
    virtual bool atapi_request_sense(const uint8_t *cmd);
    virtual bool atapi_read_capacity(const uint8_t *cmd);
    
    // ATAPI mode pages
    virtual size_t atapi_get_mode_page(uint8_t page_ctrl, uint8_t page_idx, uint8_t *buffer, size_t max_bytes);
};