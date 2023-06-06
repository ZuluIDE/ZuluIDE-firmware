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

#include "ide_atapi.h"
#include "ide_utils.h"
#include "atapi_constants.h"
#include "ZuluIDE.h"
#include "ZuluIDE_config.h"

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

bool IDEATAPIDevice::handle_command(ide_registers_t *regs)
{
    switch (regs->command)
    {
        // Commands superseded by the ATAPI packet interface
        case IDE_CMD_IDENTIFY_DEVICE:
        case IDE_CMD_EXECUTE_DEVICE_DIAGNOSTIC:
        case IDE_CMD_DEVICE_RESET:
        case IDE_CMD_READ_SECTORS:
        case IDE_CMD_READ_SECTORS_EXT:
            return set_packet_device_signature(IDE_ERROR_ABORT, false);

        // Supported IDE commands
        case IDE_CMD_SET_FEATURES: return cmd_set_features(regs);
        case IDE_CMD_IDENTIFY_PACKET_DEVICE: return cmd_identify_packet_device(regs);
        case IDE_CMD_PACKET: return cmd_packet(regs);
        default: return false;
    }
}

void IDEATAPIDevice::handle_event(ide_event_t evt)
{
    if (evt == IDE_EVENT_HWRST || evt == IDE_EVENT_SWRST)
    {
        set_packet_device_signature(0, true);
    }
}

// Set configuration based on register contents
bool IDEATAPIDevice::cmd_set_features(ide_registers_t *regs)
{
    uint8_t feature = regs->feature;

    regs->error = 0;
    if (feature == IDE_SET_FEATURE_TRANSFER_MODE)
    {
        uint8_t mode = regs->sector_count;
        dbgmsg("-- Set transfer mode ", mode);
    }
    else if (feature == IDE_SET_FEATURE_DISABLE_REVERT_TO_POWERON)
    {
        dbgmsg("-- Disable revert to power-on defaults");
    }
    else if (feature == IDE_SET_FEATURE_ENABLE_REVERT_TO_POWERON)
    {
        dbgmsg("-- Enable revert to power-on defaults");
    }
    else
    {
        dbgmsg("-- Unknown SET_FEATURE: ", feature);
        regs->error = IDE_ERROR_ABORT;
    }

    ide_phy_set_regs(regs);
    if (regs->error == 0)
    {
        ide_phy_assert_irq(IDE_STATUS_DEVRDY);
    }
    else
    {
        ide_phy_assert_irq(IDE_STATUS_DEVRDY | IDE_STATUS_ERR);
    }

    return true;
}

// "ATAPI devices shall swap bytes for ASCII fields to maintain compatibility with ATA."
static void copy_id_string(uint16_t *dst, size_t maxwords, const char *src)
{
    for (size_t i = 0; i < maxwords; i++)
    {
        uint8_t b0 = (*src != 0) ? (*src++) : ' ';
        uint8_t b1 = (*src != 0) ? (*src++) : ' ';

        dst[i] = ((uint16_t)b0 << 8) | b1;
    }
}

// Responds with 512 bytes of identification data
bool IDEATAPIDevice::cmd_identify_packet_device(ide_registers_t *regs)
{
    uint16_t idf[256] = {0};

    idf[IDE_IDENTIFY_OFFSET_GENERAL_CONFIGURATION] = 0x8000 | (m_devinfo.devtype << 8) | (m_devinfo.removable ? 0x80 : 0); // Device type
    idf[IDE_IDENTIFY_OFFSET_GENERAL_CONFIGURATION] |= (1 << 5); // Interrupt DRQ mode
    idf[IDE_IDENTIFY_OFFSET_CAPABILITIES_1] = 0x0200; // LBA supported
    idf[IDE_IDENTIFY_OFFSET_STANDARD_VERSION_MAJOR] = 0x0078; // Version ATAPI-6
    idf[IDE_IDENTIFY_OFFSET_COMMAND_SET_SUPPORT_1] = 0x0014; // PACKET, Removable device command sets supported
    idf[IDE_IDENTIFY_OFFSET_COMMAND_SET_SUPPORT_2] = 0x4000;
    idf[IDE_IDENTIFY_OFFSET_COMMAND_SET_SUPPORT_3] = 0x4000;
    idf[IDE_IDENTIFY_OFFSET_COMMAND_SET_ENABLED_1] = 0x0014;
    idf[IDE_IDENTIFY_OFFSET_HARDWARE_RESET_RESULT] = 0x4049; // Diagnostics results

    const char *fwrev = ZULU_FW_VERSION;
    const char *model_name = "ZuluIDE CDROM";
    copy_id_string(&idf[IDE_IDENTIFY_OFFSET_FIRMWARE_REV], 4, fwrev);
    copy_id_string(&idf[IDE_IDENTIFY_OFFSET_MODEL_NUMBER], 20, model_name);

    // Calculate checksum
    // See 8.15.61 Word 255: Integrity word
    uint8_t checksum = 0xA5;
    for (int i = 0; i < 255; i++)
    {
        checksum += (idf[i] & 0xFF) + (idf[i] >> 8);
    }
    checksum = -checksum;
    idf[IDE_IDENTIFY_OFFSET_INTEGRITY_WORD] = ((uint16_t)checksum << 8) | 0xA5;

    ide_phy_start_write(sizeof(idf));
    ide_phy_write_block((uint8_t*)idf, sizeof(idf));
    
    uint32_t start = millis();
    while (!ide_phy_is_write_finished())
    {
        if ((uint32_t)(millis() - start) > 10000)
        {
            logmsg("IDEATAPIDevice::cmd_identify_packet_device() response write timeout");
            ide_phy_stop_transfers();
            return false;
        }
    }

    regs->error = 0;
    ide_phy_set_regs(regs);
    ide_phy_assert_irq(IDE_STATUS_DEVRDY);
    return true;
}

bool IDEATAPIDevice::cmd_packet(ide_registers_t *regs)
{
    // Host gives limit to bytecount in responses
    m_atapi_state.data_state = ATAPI_DATA_IDLE;
    m_atapi_state.bytes_req = regs->lba_mid | ((uint16_t)regs->lba_high << 8);

    // Check if PHY has already received command
    if (!ide_phy_can_read_block() && (regs->status & IDE_STATUS_BSY))
    {
        dbgmsg("-- Starting ATAPI PACKET command read");
        // Report ready to receive command, keep BSY still high
        regs->sector_count = ATAPI_SCOUNT_IS_CMD; // Command transfer to device
        ide_phy_set_regs(regs);

        // Start the data transfer and clear BSY
        ide_phy_start_read(12);
    }

    uint32_t start = millis();
    while (!ide_phy_can_read_block())
    {
        if ((uint32_t)(millis() - start) > 10000)
        {
            logmsg("IDEATAPIDevice::cmd_packet() command read timeout");
            ide_phy_stop_transfers();
            return false;
        }
    }

    uint8_t cmdbuf[12] = {0};
    ide_phy_read_block(cmdbuf, sizeof(cmdbuf));

    dbgmsg("-- ATAPI command: ", get_atapi_command_name(cmdbuf[0]), " ", bytearray(cmdbuf, 12));
    return handle_atapi_command(cmdbuf);
}

// Set the packet device signature values to PHY registers
// See T13/1410D revision 3a section 9.12 Signature and persistence
bool IDEATAPIDevice::set_packet_device_signature(uint8_t error, bool was_reset)
{
    ide_registers_t regs = {};
    ide_phy_get_regs(&regs);

    regs.error = error;
    regs.lba_low = 0x01;
    regs.lba_mid = 0x14;
    regs.lba_high = 0xEB;
    regs.sector_count = 0x01;
    
    if (was_reset)
        regs.status = 0;
    else
        regs.status = IDE_STATUS_BSY;
    ide_phy_set_regs(&regs);

    if (!was_reset)
    {
        // Command complete
        ide_phy_assert_irq(IDE_STATUS_DEVRDY);
    }

    return true;
}

bool IDEATAPIDevice::atapi_send_data(const uint8_t *data, size_t blocksize, size_t num_blocks, bool wait_finish)
{
    dbgmsg("---- ATAPI send ", (int)num_blocks, "x", (int)blocksize, " bytes: ",
           bytearray((const uint8_t*)data, blocksize * num_blocks));

    size_t max_blocksize = ide_phy_get_max_blocksize();
    if (blocksize > max_blocksize)
    {
        // Have to split the blocks for phy
        size_t split = (blocksize + max_blocksize - 1) / max_blocksize;
        assert(blocksize % split == 0);
        blocksize /= split;
        num_blocks *= split;
    }

    for (size_t i = 0; i < num_blocks; i++)
    {
        if (!atapi_send_data_block(data + blocksize * i, blocksize))
        {
            return false;
        }
    }

    if (wait_finish)
    {
        return atapi_send_wait_finish();
    }
    else
    {
        return true;
    }
}

bool IDEATAPIDevice::atapi_send_data_block(const uint8_t *data, uint16_t blocksize)
{
    // dbgmsg("---- Send data block ", (uint32_t)data, " ", (int)blocksize);

    if (m_atapi_state.data_state != ATAPI_DATA_WRITE
        || blocksize != m_atapi_state.blocksize)
    {
        m_atapi_state.blocksize = blocksize;
        m_atapi_state.data_state = ATAPI_DATA_WRITE;

        // Set number bytes to transfer to registers
        ide_registers_t regs = {};
        ide_phy_get_regs(&regs);
        regs.status = IDE_STATUS_BSY;
        regs.sector_count = ATAPI_SCOUNT_TO_HOST; // Data transfer to host
        regs.lba_mid = (uint8_t)blocksize;
        regs.lba_high = (uint8_t)(blocksize >> 8);
        ide_phy_set_regs(&regs);

        // Start data transfer
        ide_phy_start_write(blocksize);
        ide_phy_write_block(data, blocksize);
    }
    else
    {
        // Add block to existing transfer
        uint32_t start = millis();
        while (!ide_phy_can_write_block())
        {
            platform_poll();
            if ((uint32_t)(millis() - start) > 10000)
            {
                logmsg("IDEATAPIDevice::atapi_send_data_block() data write timeout");
                return false;
            }
        }

        ide_phy_write_block(data, blocksize);
    }

    return true;
}

bool IDEATAPIDevice::atapi_send_wait_finish()
{
    // Wait for transfer to finish
    uint32_t start = millis();
    while (!ide_phy_is_write_finished())
    {
        platform_poll();
        if ((uint32_t)(millis() - start) > 10000)
        {
            logmsg("IDEATAPIDevice::atapi_send_wait_finish() data write timeout");
            return false;
        }
    }

    return true;
}

bool IDEATAPIDevice::handle_atapi_command(const uint8_t *cmd)
{
    switch (cmd[0])
    {
        case ATAPI_CMD_TEST_UNIT_READY: return atapi_test_unit_ready(cmd);
        case ATAPI_CMD_INQUIRY:         return atapi_inquiry(cmd);
        case ATAPI_CMD_MODE_SENSE10:    return atapi_mode_sense(cmd);
        case ATAPI_CMD_REQUEST_SENSE:   return atapi_request_sense(cmd);
        case ATAPI_CMD_GET_EVENT_STATUS_NOTIFICATION: return atapi_get_event_status_notification(cmd);
        case ATAPI_CMD_READ_CAPACITY:   return atapi_read_capacity(cmd);
        case ATAPI_CMD_READ6:           return atapi_read(cmd);
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
    dbgmsg("-- ATAPI error: ", sense_key, " ", sense_asc);
    m_atapi_state.sense_key = sense_key;
    m_atapi_state.sense_asc = sense_asc;
    m_atapi_state.data_state = ATAPI_DATA_IDLE;

    ide_registers_t regs = {};
    ide_phy_get_regs(&regs);
    regs.error = IDE_ERROR_ABORT | (sense_key << 4);
    regs.sector_count = ATAPI_SCOUNT_IS_CMD | ATAPI_SCOUNT_TO_HOST;
    ide_phy_set_regs(&regs);
    ide_phy_assert_irq(IDE_STATUS_DEVRDY | IDE_STATUS_ERR);

    return true;
}

bool IDEATAPIDevice::atapi_cmd_ok()
{
    dbgmsg("-- ATAPI success");
    m_atapi_state.sense_key = 0;
    m_atapi_state.sense_asc = 0;
    m_atapi_state.data_state = ATAPI_DATA_IDLE;

    ide_registers_t regs = {};
    ide_phy_get_regs(&regs);
    regs.error = 0;
    regs.sector_count = ATAPI_SCOUNT_IS_CMD | ATAPI_SCOUNT_TO_HOST;
    ide_phy_set_regs(&regs);
    ide_phy_assert_irq(IDE_STATUS_DEVRDY);

    return true;
}

bool IDEATAPIDevice::atapi_test_unit_ready(const uint8_t *cmd)
{
    return atapi_cmd_ok();
}

bool IDEATAPIDevice::atapi_inquiry(const uint8_t *cmd)
{
    uint8_t req_bytes = cmd[4];
    uint8_t inquiry[36] = {0};
    uint8_t count = 36;
    inquiry[ATAPI_INQUIRY_OFFSET_TYPE] = m_devinfo.devtype;
    inquiry[ATAPI_INQUIRY_REMOVABLE_MEDIA] = m_devinfo.removable ? 0x80 : 0;
    inquiry[ATAPI_INQUIRY_ATAPI_VERSION] = 0x21;
    inquiry[ATAPI_INQUIRY_EXTRA_LENGTH] = count - 5;
    memcpy(&inquiry[ATAPI_INQUIRY_VENDOR], "Vendor", 6);
    memcpy(&inquiry[ATAPI_INQUIRY_PRODUCT], "Product", 7);

    if (req_bytes < count) count = req_bytes;
    atapi_send_data(inquiry, count);

    return atapi_cmd_ok();
}

bool IDEATAPIDevice::atapi_mode_sense(const uint8_t *cmd)
{
    uint8_t page_ctrl = cmd[2] >> 6;
    uint8_t page_idx = cmd[2] & 0x3F;
    uint8_t req_bytes = parse_be16(&cmd[7]);

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

    atapi_send_data(m_buffer.bytes, resp_bytes);

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
    write_be16(&resp[12], m_atapi_state.sense_asc);
    
    if (req_bytes < sense_length) sense_length = req_bytes;
    atapi_send_data(m_buffer.bytes, sense_length);

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
        if (!atapi_send_data(m_buffer.bytes, 8))
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
        atapi_send_data(m_buffer.bytes, 4);
        return atapi_cmd_ok();
    }
}

bool IDEATAPIDevice::atapi_read_capacity(const uint8_t *cmd)
{
    uint64_t capacity = (m_image ? m_image->capacity() : 0);
    uint8_t *buf = m_buffer.bytes;
    write_be32(&buf[0], capacity / m_devinfo.bytes_per_sector);
    write_be32(&buf[4], m_devinfo.bytes_per_sector);
    atapi_send_data(buf, 8);

    return atapi_cmd_ok();
}

bool IDEATAPIDevice::atapi_read(const uint8_t *cmd)
{
    uint32_t lba, transfer_len;
    if (cmd[0] == ATAPI_CMD_READ6)
    {
        lba = parse_be24(&cmd[1]) & 0x1FFFFF;
        transfer_len = cmd[4];
        if (transfer_len == 0) transfer_len = 256;
    }
    else if (cmd[0] == ATAPI_CMD_READ10)
    {
        lba = parse_be32(&cmd[2]);
        transfer_len = parse_be16(&cmd[7]);
    }
    else if (cmd[0] == ATAPI_CMD_READ12)
    {
        lba = parse_be32(&cmd[2]);
        transfer_len = parse_be32(&cmd[6]);
    }
    else
    {
        assert(false);
    }

    if (!m_image)
    {
        return atapi_cmd_error(ATAPI_SENSE_NOT_READY, ATAPI_ASC_NO_MEDIUM);
    }

    dbgmsg("-- Read ", (int)transfer_len, " sectors starting at ", (int)lba);
    return doRead(lba, transfer_len);
}

bool IDEATAPIDevice::doRead(uint32_t lba, uint32_t transfer_len)
{
    // TODO: asynchronous transfer
    // for (int i = 0; i < ATAPI_TRANSFER_REQ_COUNT; i++)
    // {
    //     m_transfer_reqs[i] = ide_phy_msg_t{};
    //     m_transfer_reqs[i].status = &m_transfer_req_status[i];
    //     m_transfer_reqs[i].type = IDE_MSG_SEND_DATA;
    // }

    bool status = m_image->read((uint64_t)lba * m_devinfo.bytes_per_sector,
                                m_devinfo.bytes_per_sector, transfer_len,
                                this);
    
    if (status)
    {
        return atapi_send_wait_finish() && atapi_cmd_ok();
    }
    else
    {
        return atapi_cmd_error(ATAPI_SENSE_MEDIUM_ERROR, 0);
    }
}

ssize_t IDEATAPIDevice::read_callback(const uint8_t *data, size_t blocksize, size_t num_blocks)
{
    platform_poll();
    if (!atapi_send_data(data, blocksize, num_blocks, false))
    {
        return -1;
    }
    else
    {
        return num_blocks;
    }
}

bool IDEATAPIDevice::atapi_write(const uint8_t *cmd)
{
    return atapi_cmd_error(ATAPI_SENSE_ABORTED_CMD, ATAPI_ASC_WRITE_PROTECTED);
}

ssize_t IDEATAPIDevice::write_callback(uint8_t *data, size_t blocksize, size_t num_blocks)
{
    assert(false);
    return -1;
}

size_t IDEATAPIDevice::atapi_get_mode_page(uint8_t page_ctrl, uint8_t page_idx, uint8_t *buffer, size_t max_bytes)
{
    return 0;
}