// Implements IDE command handlers for generic ATAPI device.

#pragma once

#include "ide_protocol.h"
#include "ide_imagefile.h"
#include <stddef.h>

// Number of simultaneous transfer requests to pass to ide_phy.
#define ATAPI_TRANSFER_REQ_COUNT 2

// Generic ATAPI device implementation: encapsulated SCSI commands over ATA.
// Abstract class, use one of the subclasses (IDECDROMDevice)
class IDEATAPIDevice: public IDEDevice, public IDEImage::Callback
{
public:
    IDEATAPIDevice();

    virtual void set_image(IDEImage *image);

    virtual void poll();

    virtual bool handle_command(ide_registers_t *regs);

    virtual void handle_event(ide_event_t event);

    virtual bool is_packet_device() { return true; }

protected:
    IDEImage *m_image;

    // Device type info is filled in by subclass
    struct {
        uint8_t devtype;
        bool removable;
        uint32_t bytes_per_sector;
        uint8_t media_status_events;
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

    // // Data transfer state
    // volatile ide_msg_status_t m_transfer_req_status[ATAPI_TRANSFER_REQ_COUNT];
    // ide_phy_msg_t m_transfer_reqs[ATAPI_TRANSFER_REQ_COUNT];
    
    // IDE command handlers
    virtual bool cmd_set_features(ide_registers_t *regs);
    virtual bool cmd_identify_packet_device(ide_registers_t *regs);
    virtual bool cmd_packet(ide_registers_t *regs);
    virtual bool set_packet_device_signature(uint8_t error, bool was_reset);
    
    // Methods used by ATAPI command implementations
    bool set_atapi_byte_count(uint16_t byte_count);
    bool atapi_send_data(const uint8_t *data, uint16_t byte_count);
    bool atapi_cmd_error(uint8_t sense_key, uint16_t sense_asc);
    bool atapi_cmd_ok();

    // ATAPI command handlers
    virtual bool handle_atapi_command(const uint8_t *cmd);
    virtual bool atapi_test_unit_ready(const uint8_t *cmd);
    virtual bool atapi_inquiry(const uint8_t *cmd);
    virtual bool atapi_mode_sense(const uint8_t *cmd);
    virtual bool atapi_request_sense(const uint8_t *cmd);
    virtual bool atapi_get_event_status_notification(const uint8_t *cmd);
    virtual bool atapi_read_capacity(const uint8_t *cmd);
    
    // Read handlers
    virtual bool atapi_read(const uint8_t *cmd);
    virtual ssize_t read_callback(const uint8_t *data, size_t bytes);
    
    // Write handlers
    virtual bool atapi_write(const uint8_t *cmd);
    virtual ssize_t write_callback(uint8_t *data, size_t bytes);

    // ATAPI mode pages
    virtual size_t atapi_get_mode_page(uint8_t page_ctrl, uint8_t page_idx, uint8_t *buffer, size_t max_bytes);
};