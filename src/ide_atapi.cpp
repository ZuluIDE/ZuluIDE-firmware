#include "ide_atapi.h"
#include "atapi_constants.h"
#include "ZuluIDE.h"

// Map from command index for command name for logging
static const char *get_atapi_command_name(uint8_t cmd)
{
    switch (cmd)
    {
#define CMD_NAME_TO_STR(name, code) case code: return #name;
    ATAPI_COMMAND_LIST(CMD_NAME_TO_STR)
#undef CMD_NAME_TO_STR
        default: return "UNKNOWN_CMD";
    }
}

IDEATAPIDevice::IDEATAPIDevice():
    m_devinfo({}), m_atapi_state({})
{

}

void IDEATAPIDevice::set_image(IDEImage *image)
{
    m_image = image;
}

void IDEATAPIDevice::poll()
{

}

bool IDEATAPIDevice::handle_command(ide_phy_msg_t *msg)
{
    if (msg->type != IDE_MSG_CMD_START) return false;
    switch (msg->payload.cmd_start.command)
    {
        // Commands superseded by the ATAPI packet interface
        case IDE_CMD_IDENTIFY_DEVICE:
        case IDE_CMD_EXECUTE_DEVICE_DIAGNOSTIC:
        case IDE_CMD_DEVICE_RESET:
        case IDE_CMD_READ_SECTORS:
        case IDE_CMD_READ_SECTORS_EXT:
            return set_packet_device_signature(IDE_ERROR_ABORT);

        case IDE_CMD_IDENTIFY_PACKET_DEVICE: return cmd_identify_packet_device(msg);
        case IDE_CMD_PACKET: return cmd_packet(msg);
        default: return false;
    }
}

void IDEATAPIDevice::handle_event(ide_phy_msg_t *msg)
{
    if (msg->type == IDE_MSG_RESET)
    {
        set_packet_device_signature(0);
    }
}

// Responds with 512 bytes of identification data
bool IDEATAPIDevice::cmd_identify_packet_device(ide_phy_msg_t *msg)
{
    uint16_t idf[256] = {0};
    volatile ide_msg_status_t status = IDE_MSGSTAT_IDLE;

    idf[IDE_IDENTIFY_OFFSET_GENERAL_CONFIGURATION] = 0x8000 | (m_devinfo.devtype << 8) | (m_devinfo.removable ? 0x80 : 0); // Device type
    idf[IDE_IDENTIFY_OFFSET_STANDARD_VERSION_MAJOR] = 0x0078; // Version ATAPI-6
    idf[IDE_IDENTIFY_OFFSET_COMMAND_SET_SUPPORT_1] = 0x0014; // PACKET, Removable device command sets supported
    idf[IDE_IDENTIFY_OFFSET_COMMAND_SET_SUPPORT_2] = 0x4000;
    idf[IDE_IDENTIFY_OFFSET_COMMAND_SET_SUPPORT_3] = 0x4000;
    idf[IDE_IDENTIFY_OFFSET_COMMAND_SET_ENABLED_1] = 0x0014;

    // Calculate checksum
    // See 8.15.61 Word 255: Integrity word
    uint8_t checksum = 0xA5;
    for (int i = 0; i < 255; i++)
    {
        checksum += (idf[i] & 0xFF) + (idf[i] >> 8);
    }
    checksum = -checksum;
    idf[IDE_IDENTIFY_OFFSET_INTEGRITY_WORD] = ((uint16_t)checksum << 8) | 0xA5;

    ide_phy_msg_t response = {};
    response.status = &status;
    response.type = IDE_MSG_SEND_DATA;
    response.payload.send_data.data = idf;
    response.payload.send_data.words = 256;
    ide_phy_send_msg(&response);

    while (!(status & IDE_MSGSTAT_DONE))
    {
        delay(1);
    }

    response = ide_phy_msg_t{};
    response.type = IDE_MSG_DEVICE_RDY;
    response.payload.device_rdy.error = 0;
    return ide_phy_send_msg(&response);
}

bool IDEATAPIDevice::cmd_packet(ide_phy_msg_t *msg)
{
    // Host gives limit to bytecount in responses
    m_atapi_state.bytes_req = msg->payload.cmd_start.lbamid | ((uint16_t)msg->payload.cmd_start.lbahigh << 8);

    // Report ready to receive command, keep BSY still high
    ide_phy_msg_t response = {};
    response.type = IDE_MSG_DEVICE_RDY;
    response.payload.device_rdy.status = IDE_STATUS_DEVRDY | IDE_STATUS_BSY;
    response.payload.device_rdy.set_registers = true;
    response.payload.device_rdy.sector_count = ATAPI_SCOUNT_IS_CMD; // Command transfer to device
    response.payload.device_rdy.lbamid = msg->payload.cmd_start.lbamid;
    response.payload.device_rdy.lbahigh = msg->payload.cmd_start.lbahigh;
    ide_phy_send_msg(&response);

    // Start the data transfer and clear BSY
    uint16_t cmdbuf[6] = {0};
    volatile ide_msg_status_t status = IDE_MSGSTAT_IDLE;
    response = ide_phy_msg_t{};
    response.type = IDE_MSG_RECV_DATA;
    response.status = &status;
    response.payload.recv_data.words = 6;
    response.payload.recv_data.data = cmdbuf;
    ide_phy_send_msg(&response);

    while (!(status & IDE_MSGSTAT_DONE))
    {
        delay(1);
    }

    __sync_synchronize(); // Make sure data buffer writes have committed

    return handle_atapi_command((const uint8_t*)cmdbuf);
}

// Set the packet device signature values to PHY registers
// See T13/1410D revision 3a section 9.12 Signature and persistence
bool IDEATAPIDevice::set_packet_device_signature(uint8_t error)
{
    ide_phy_msg_t msg = {};
    msg.type = IDE_MSG_DEVICE_RDY;
    msg.payload.device_rdy.error = error;
    msg.payload.device_rdy.set_registers = true;
    msg.payload.device_rdy.device = 0x00;
    msg.payload.device_rdy.lbalow = 0x01;
    msg.payload.device_rdy.lbamid = 0x14;
    msg.payload.device_rdy.lbahigh = 0xEB;
    msg.payload.device_rdy.sector_count = 0x01;
    return ide_phy_send_msg(&msg);
}

bool IDEATAPIDevice::atapi_send_data(const uint16_t *data, uint16_t byte_count)
{
    volatile ide_msg_status_t status = IDE_MSGSTAT_IDLE;

    azdbg("-- ATAPI send ", (int)byte_count, " bytes: ", bytearray((const uint8_t*)data, byte_count));

    // Set number bytes to transfer to registers
    ide_phy_msg_t response = {};
    response.type = IDE_MSG_DEVICE_RDY;
    response.payload.device_rdy.status = IDE_STATUS_DEVRDY | IDE_STATUS_BSY;
    response.payload.device_rdy.set_registers = true;
    response.payload.device_rdy.sector_count = ATAPI_SCOUNT_TO_HOST; // Data transfer to host
    response.payload.device_rdy.lbamid = (uint8_t)byte_count;
    response.payload.device_rdy.lbahigh = (uint8_t)(byte_count >> 8);
    if (!ide_phy_send_msg(&response)) return false;

    response = ide_phy_msg_t{};
    response.status = &status;
    response.type = IDE_MSG_SEND_DATA;
    response.payload.send_data.data = data;
    response.payload.send_data.words = (byte_count + 1) / 2;
    response.payload.send_data.assert_irq = true;
    if (!ide_phy_send_msg(&response)) return false;

    while (!(status & IDE_MSGSTAT_DONE))
    {
        delay(1);
    }

    if (status != IDE_MSGSTAT_SUCCESS)
    {
        azdbg("---- Send data failed: ", (uint8_t)status);
    }

    return true;
}

bool IDEATAPIDevice::handle_atapi_command(const uint8_t *cmd)
{
    azdbg("-- ATAPI command: ", get_atapi_command_name(cmd[0]), " ", bytearray(cmd, 12));

    switch (cmd[0])
    {
        case ATAPI_CMD_TEST_UNIT_READY: return atapi_test_unit_ready(cmd);
        case ATAPI_CMD_INQUIRY:         return atapi_inquiry(cmd);
        case ATAPI_CMD_MODE_SENSE10:    return atapi_mode_sense(cmd);
        case ATAPI_CMD_REQUEST_SENSE:   return atapi_request_sense(cmd);
        case ATAPI_CMD_GET_EVENT_STATUS_NOTIFICATION: return atapi_get_event_status_notification(cmd);
        case ATAPI_CMD_READ_CAPACITY:   return atapi_read_capacity(cmd);
        case ATAPI_CMD_READ10:          return atapi_read(cmd);
        case ATAPI_CMD_READ12:          return atapi_read(cmd);
        case ATAPI_CMD_WRITE10:         return atapi_write(cmd);
        case ATAPI_CMD_WRITE12:         return atapi_write(cmd);

        default:
            return atapi_cmd_error(ATAPI_SENSE_ILLEGAL_REQ, ATAPI_ASC_INVALID_CMD);
    }
}

bool IDEATAPIDevice::atapi_cmd_error(uint8_t sense_key, uint16_t sense_asc)
{
    azdbg("-- Reporting ATAPI error: ", sense_key, " ", sense_asc);
    m_atapi_state.sense_key = sense_key;
    m_atapi_state.sense_asc = sense_asc;

    ide_phy_msg_t msg = {};
    msg.type = IDE_MSG_DEVICE_RDY;
    msg.payload.device_rdy.error = IDE_ERROR_ABORT | (sense_key << 4);
    msg.payload.device_rdy.status = IDE_STATUS_DEVRDY | IDE_STATUS_ERR;
    msg.payload.device_rdy.set_registers = true;
    msg.payload.device_rdy.sector_count = ATAPI_SCOUNT_IS_CMD | ATAPI_SCOUNT_TO_HOST;
    msg.payload.device_rdy.assert_irq = true;
    return ide_phy_send_msg(&msg);
}

bool IDEATAPIDevice::atapi_cmd_ok()
{
    azdbg("-- Command completed ok");
    m_atapi_state.sense_key = 0;
    m_atapi_state.sense_asc = 0;

    ide_phy_msg_t msg = {};
    msg.type = IDE_MSG_DEVICE_RDY;
    msg.payload.device_rdy.error = 0;
    msg.payload.device_rdy.status = IDE_STATUS_DEVRDY;
    msg.payload.device_rdy.set_registers = true;
    msg.payload.device_rdy.sector_count = ATAPI_SCOUNT_IS_CMD | ATAPI_SCOUNT_TO_HOST;
    msg.payload.device_rdy.assert_irq = true;
    return ide_phy_send_msg(&msg);
}

bool IDEATAPIDevice::atapi_test_unit_ready(const uint8_t *cmd)
{
    return atapi_cmd_ok();
}

bool IDEATAPIDevice::atapi_inquiry(const uint8_t *cmd)
{
    uint8_t req_bytes = cmd[4];
    uint16_t inquiry_buf[18] = {0};
    uint8_t *inquiry = (uint8_t*)inquiry_buf;
    uint8_t count = 36;
    inquiry[ATAPI_INQUIRY_OFFSET_TYPE] = m_devinfo.devtype;
    inquiry[ATAPI_INQUIRY_REMOVABLE_MEDIA] = m_devinfo.removable ? 0x80 : 0;
    inquiry[ATAPI_INQUIRY_ATAPI_VERSION] = 0x21;
    inquiry[ATAPI_INQUIRY_EXTRA_LENGTH] = count - 5;
    memcpy(&inquiry[ATAPI_INQUIRY_VENDOR], "Vendor", 6);
    memcpy(&inquiry[ATAPI_INQUIRY_PRODUCT], "Product", 7);

    if (req_bytes < count) count = req_bytes;
    atapi_send_data(inquiry_buf, count);

    return atapi_cmd_ok();
}

bool IDEATAPIDevice::atapi_mode_sense(const uint8_t *cmd)
{
    uint8_t page_ctrl = cmd[2] >> 6;
    uint8_t page_idx = cmd[2] & 0x3F;
    uint8_t req_bytes = ((uint16_t)cmd[7] << 8) | cmd[8];

    uint8_t *resp = m_buffer.bytes;
    size_t max_bytes = sizeof(m_buffer);
    if (req_bytes < max_bytes) max_bytes = req_bytes;
    size_t resp_bytes = 0;

    if (page_idx != 0x3F)
    {
        // Request single page
        resp_bytes += atapi_get_mode_page(page_ctrl, page_idx, &resp[resp_bytes], max_bytes - resp_bytes);
    }
    else
    {
        // Request all pages
        for (int i = 0x01; i < 0x3F && resp_bytes < max_bytes; i++)
        {
            resp_bytes += atapi_get_mode_page(page_ctrl, page_idx, &resp[resp_bytes], max_bytes - resp_bytes);
        }
        resp_bytes += atapi_get_mode_page(page_ctrl, 0, &resp[resp_bytes], max_bytes - resp_bytes);
    }

    atapi_send_data(m_buffer.word, resp_bytes);

    return atapi_cmd_ok();
}

bool IDEATAPIDevice::atapi_request_sense(const uint8_t *cmd)
{
    uint8_t req_bytes = cmd[4];

    uint8_t *resp = m_buffer.bytes;
    size_t sense_length = 18;
    memset(resp, 0, sense_length);
    resp[0] = 0x80 | (m_atapi_state.sense_key != 0 ? 0x70 : 0);
    resp[2] = m_atapi_state.sense_key;
    resp[7] = sense_length - 7;
    resp[12] = m_atapi_state.sense_asc >> 8;
    resp[13] = m_atapi_state.sense_asc & 0xFF;
    
    if (req_bytes < sense_length) sense_length = req_bytes;
    atapi_send_data(m_buffer.word, sense_length);

    return atapi_cmd_ok();
}

bool IDEATAPIDevice::atapi_get_event_status_notification(const uint8_t *cmd)
{
    if (!(cmd[1] & 1))
    {
        // Async. notification is not supported
        return atapi_cmd_error(ATAPI_SENSE_ILLEGAL_REQ, ATAPI_ASC_INVALID_FIELD);
    }
    else if (m_devinfo.media_status_events)
    {
        // Report media status events
        uint8_t *buf = m_buffer.bytes;
        buf[0] = 0;
        buf[1] = 6; // EventDataLength
        buf[2] = 0x04; // Media status events
        buf[3] = 0x04; // Supported events
        buf[4] = m_devinfo.media_status_events;
        buf[5] = 0x01; // Power status
        buf[6] = 0; // Start slot
        buf[7] = 0; // End slot
        if (!atapi_send_data(m_buffer.word, 8))
        {
            return atapi_cmd_error(ATAPI_SENSE_ABORTED_CMD, 0);
        }

        m_devinfo.media_status_events = 0;
        return atapi_cmd_ok();
    }
    else
    {
        // No events to report
        uint8_t *buf = m_buffer.bytes;
        buf[0] = 0;
        buf[1] = 2; // EventDataLength
        buf[2] = 0x00; // Media status events
        buf[3] = 0x04; // Supported events
        atapi_send_data(m_buffer.word, 4);
        return atapi_cmd_ok();
    }
}

bool IDEATAPIDevice::atapi_read_capacity(const uint8_t *cmd)
{
    uint64_t capacity = (m_image ? m_image->capacity() : 0);

    m_buffer.dword[0] = __builtin_bswap32(capacity / m_devinfo.bytes_per_sector);
    m_buffer.dword[1] = __builtin_bswap32(m_devinfo.bytes_per_sector);
    atapi_send_data(m_buffer.word, 8);

    return atapi_cmd_ok();
}

bool IDEATAPIDevice::atapi_read(const uint8_t *cmd)
{
    uint32_t lba, transfer_len;
    if (cmd[0] == ATAPI_CMD_READ10)
    {
        lba = ((uint32_t)cmd[2] << 24)
            | ((uint32_t)cmd[3] << 16)
            | ((uint32_t)cmd[4] << 8)
            | ((uint32_t)cmd[5] << 0);
        
        transfer_len = ((uint32_t)cmd[7] << 8)
                     | ((uint32_t)cmd[8] << 0);
    }
    else
    {
        lba = ((uint32_t)cmd[2] << 24)
            | ((uint32_t)cmd[3] << 16)
            | ((uint32_t)cmd[4] << 8)
            | ((uint32_t)cmd[5] << 0);
        
        transfer_len = ((uint32_t)cmd[6] << 24)
                     | ((uint32_t)cmd[7] << 16)
                     | ((uint32_t)cmd[8] << 8)
                     | ((uint32_t)cmd[9] << 0);
    }

    if (!m_image)
    {
        return atapi_cmd_error(ATAPI_SENSE_NOT_READY, ATAPI_ASC_NO_MEDIUM);
    }

    // for (int i = 0; i < ATAPI_TRANSFER_REQ_COUNT; i++)
    // {
    //     m_transfer_reqs[i] = ide_phy_msg_t{};
    //     m_transfer_reqs[i].status = &m_transfer_req_status[i];
    //     m_transfer_reqs[i].type = IDE_MSG_SEND_DATA;
    // }

    bool status = m_image->read((uint64_t)lba * m_devinfo.bytes_per_sector,
                                transfer_len * m_devinfo.bytes_per_sector,
                                this);
    
    if (status)
    {
        return atapi_cmd_ok();
    }
    else
    {
        return atapi_cmd_error(ATAPI_SENSE_MEDIUM_ERROR, 0);
    }
}

ssize_t IDEATAPIDevice::read_callback(const uint8_t *data, size_t bytes)
{
    // TODO: Make this asynchronous for optimization
    bytes &= ~1;
    if (!atapi_send_data((const uint16_t*)data, bytes))
    {
        return -1;
    }
    else
    {
        return bytes;
    }
}

bool IDEATAPIDevice::atapi_write(const uint8_t *cmd)
{
    return atapi_cmd_error(ATAPI_SENSE_ABORTED_CMD, ATAPI_ASC_WRITE_PROTECTED);
}

ssize_t IDEATAPIDevice::write_callback(uint8_t *data, size_t bytes)
{
    assert(false);
    return -1;
}

size_t IDEATAPIDevice::atapi_get_mode_page(uint8_t page_ctrl, uint8_t page_idx, uint8_t *buffer, size_t max_bytes)
{
    return 0;
}