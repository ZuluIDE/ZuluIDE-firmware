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
#include <minIni.h>
#include <zuluide/images/image_iterator.h>

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

void IDEATAPIDevice::initialize(int devidx)
{
    memset(&m_devinfo, 0, sizeof(m_devinfo));
    memset(&m_removable, 0, sizeof(m_removable));
    m_removable.reinsert_media_after_eject = ini_getbool("IDE", "reinsert_media_after_eject", true, CONFIGFILE);
    m_removable.reinsert_media_on_inquiry = ini_getbool("IDE", "reinsert_media_on_inquiry", true, CONFIGFILE);
    m_removable.reinsert_media_after_sd_insert = ini_getbool("IDE", "reinsert_media_on_sd_insert", true, CONFIGFILE);
    m_removable.ignore_prevent_removal = ini_getbool("IDE", "ignore_prevent_removal", false, CONFIGFILE);
    if (m_removable.ignore_prevent_removal)
        logmsg("Ignoring host from preventing removal of media");
    memset(&m_atapi_state, 0, sizeof(m_atapi_state));
    IDEDevice::initialize(devidx);
}

void IDEATAPIDevice::reset()
{
    m_removable.ejected = false;
    m_removable.prevent_persistent = false;
    m_removable.prevent_removable = false;
}

void IDEATAPIDevice::set_image(IDEImage *image)
{
    m_image = image;
    // \todo disabling for now, may add back as zuluide.ini settings
    // m_atapi_state.unit_attention = true;
    // m_atapi_state.sense_asc = ATAPI_ASC_MEDIUM_CHANGE;
}

bool IDEATAPIDevice::handle_command(ide_registers_t *regs)
{
    switch (regs->command)
    {
        // Commands superseded by the ATAPI packet interface
        case IDE_CMD_IDENTIFY_DEVICE:
        case IDE_CMD_READ_SECTORS:
        case IDE_CMD_READ_SECTORS_EXT:
            return set_device_signature(IDE_ERROR_ABORT, false);
        case IDE_CMD_EXECUTE_DEVICE_DIAGNOSTIC:
            return set_device_signature(IDE_ERROR_EXEC_DEV_DIAG_DEV0_PASS, false);
        // Supported IDE commands
        case IDE_CMD_NOP: return cmd_nop(regs);
        case IDE_CMD_SET_FEATURES: return cmd_set_features(regs);
        case IDE_CMD_IDENTIFY_PACKET_DEVICE: return cmd_identify_packet_device(regs);
        case IDE_CMD_PACKET: return cmd_packet(regs);
        case IDE_CMD_DEVICE_RESET: return cmd_device_reset(regs);
        default: return false;
    }
}

void IDEATAPIDevice::handle_event(ide_event_t evt)
{
    if (evt == IDE_EVENT_HWRST || evt == IDE_EVENT_SWRST)
    {
        if (evt == IDE_EVENT_HWRST)
        {
            m_atapi_state.udma_mode = -1;
        }

        m_atapi_state.unit_attention = true;
        m_atapi_state.sense_asc = ATAPI_ASC_RESET_OCCURRED;
        set_device_signature(0, true);
    }
}

bool IDEATAPIDevice::cmd_nop(ide_registers_t *regs)
{
    // CMD_NOP always fails with CMD_ABORTED
    regs->error = IDE_ERROR_ABORT;
    ide_phy_set_regs(regs);
    ide_phy_assert_irq(IDE_STATUS_DEVRDY | IDE_STATUS_ERR);
    return true;
}

// Set configuration based on register contents
bool IDEATAPIDevice::cmd_set_features(ide_registers_t *regs)
{
    uint8_t feature = regs->feature;

    regs->error = 0;
    if (feature == IDE_SET_FEATURE_TRANSFER_MODE)
    {
        uint8_t mode = regs->sector_count;
        uint8_t mode_major = mode >> 3;
        uint8_t mode_minor = mode & 7;

        if (mode_major == 0)
        {
            m_atapi_state.udma_mode = -1;
            logmsg("-- Set PIO default transfer mode");
        }
        else if (mode_major == 1 && mode_minor <= m_phy_caps.max_pio_mode)
        {
            m_atapi_state.udma_mode = -1;
            logmsg("-- Set PIO transfer mode ", (int)mode_minor);
        }
        else if (mode_major == 8 && mode_minor <= m_phy_caps.max_udma_mode)
        {
            m_atapi_state.udma_mode = mode_minor;
            logmsg("-- Set UDMA transfer mode ", (int)mode_minor);
        }
        else
        {
            logmsg("-- Unsupported mode ", mode, " (major ", (int)mode_major, " minor ", (int)mode_minor, ")");
            regs->error = IDE_ERROR_ABORT;
        }
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

    copy_id_string(&idf[IDE_IDENTIFY_OFFSET_SERIAL_NUMBER], 10, m_devconfig.ata_serial);
    copy_id_string(&idf[IDE_IDENTIFY_OFFSET_FIRMWARE_REV], 4, m_devconfig.ata_revision);
    copy_id_string(&idf[IDE_IDENTIFY_OFFSET_MODEL_NUMBER], 20, m_devconfig.ata_model);

    idf[IDE_IDENTIFY_OFFSET_GENERAL_CONFIGURATION] = 0x8000 | (m_devinfo.devtype << 8) | (m_devinfo.removable ? 0x80 : 0); // Device type
    idf[IDE_IDENTIFY_OFFSET_GENERAL_CONFIGURATION] |= (1 << 5); // Interrupt DRQ mode
    idf[IDE_IDENTIFY_OFFSET_CAPABILITIES_1] = 0x0200; // LBA supported
    idf[IDE_IDENTIFY_OFFSET_STANDARD_VERSION_MAJOR] = 0x0078; // Version ATAPI-6
    idf[IDE_IDENTIFY_OFFSET_STANDARD_VERSION_MINOR] = 0x0019; // Minor version rev 3a
    idf[IDE_IDENTIFY_OFFSET_COMMAND_SET_SUPPORT_1] = 0x0014; // PACKET, Removable device command sets supported
    idf[IDE_IDENTIFY_OFFSET_COMMAND_SET_SUPPORT_2] = 0x4000;
    idf[IDE_IDENTIFY_OFFSET_COMMAND_SET_SUPPORT_3] = 0x4000;
    idf[IDE_IDENTIFY_OFFSET_COMMAND_SET_ENABLED_1] = 0x0014;
    idf[IDE_IDENTIFY_OFFSET_BYTE_COUNT_ZERO] = 128; // Number of bytes transferred when bytes_req = 0

    // Diagnostics results
    const ide_phy_config_t *phycfg = ide_protocol_get_config();
    if (m_devconfig.dev_index == 0)
    {
        idf[IDE_IDENTIFY_OFFSET_HARDWARE_RESET_RESULT] = 0x4009; // Device 0 passed diagnostics
        if (phycfg->enable_dev1_zeros)
        {
            idf[IDE_IDENTIFY_OFFSET_HARDWARE_RESET_RESULT] |= (1 << 6); // Device 0 responds for device 1
        }
        else
        {
            idf[IDE_IDENTIFY_OFFSET_HARDWARE_RESET_RESULT] |= 0x30; // Device 1 detected
        }
    }
    else
    {
        idf[IDE_IDENTIFY_OFFSET_HARDWARE_RESET_RESULT] = 0x4900; // Device 1 passed diagnostics
    }

    // Supported PIO modes
    if (m_phy_caps.supports_iordy) idf[IDE_IDENTIFY_OFFSET_CAPABILITIES_1] |= (1 << 11);
    idf[IDE_IDENTIFY_OFFSET_PIO_MODE_ATA1] = (m_phy_caps.max_pio_mode << 8);
    idf[IDE_IDENTIFY_OFFSET_MODE_INFO_VALID] |= 0x02; // PIO support word valid
    idf[IDE_IDENTIFY_OFFSET_MODEINFO_PIO] = (m_phy_caps.max_pio_mode >= 3) ? 1 : 0; // PIO3 supported?
    idf[IDE_IDENTIFY_OFFSET_PIO_CYCLETIME_MIN] = m_phy_caps.min_pio_cycletime_no_iordy; // Without IORDY
    idf[IDE_IDENTIFY_OFFSET_PIO_CYCLETIME_IORDY] = m_phy_caps.min_pio_cycletime_with_iordy; // With IORDY

    // Supported UDMA modes
    idf[IDE_IDENTIFY_OFFSET_MODE_INFO_VALID] |= 0x04; // UDMA support word valid
    if (m_phy_caps.max_udma_mode >= 0)
    {
        idf[IDE_IDENTIFY_OFFSET_CAPABILITIES_1] |= (1 << 8);
        idf[IDE_IDENTIFY_OFFSET_MODEINFO_ULTRADMA] = 0x0001;
        if (m_atapi_state.udma_mode == 0) idf[IDE_IDENTIFY_OFFSET_MODEINFO_ULTRADMA] |= (1 << 8);
    }

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
    m_atapi_state.dma_requested = regs->feature & 0x01;
    m_atapi_state.crc_errors = 0;

    if (m_atapi_state.dma_requested && m_atapi_state.udma_mode < 0)
    {
        dbgmsg("---- Host requested DMA transfer while DMA mode is not selected, enabling UDMA0!");
        m_atapi_state.udma_mode = 0;
    }

    if (m_atapi_state.bytes_req == 0)
    {
        // "the host should not set the byte count limit to zero. If the host sets the byte count limit to
        // zero, the contents of IDENTIFY PACKET DEVICE word 125 determines the expected behavior"
        m_atapi_state.bytes_req = 128;
    }

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

        if (ide_phy_is_command_interrupted())
        {
            dbgmsg("IDEATAPIDevice::cmd_packet() interrupted");
            ide_phy_stop_transfers();
            return false;
        }
    }

    uint8_t cmdbuf[12] = {0};
    ide_phy_read_block(cmdbuf, sizeof(cmdbuf));

    dbgmsg("-- ATAPI command: ", get_atapi_command_name(cmdbuf[0]), " ", bytearray(cmdbuf, 12));
    return handle_atapi_command(cmdbuf);
}

bool IDEATAPIDevice::cmd_device_reset(ide_registers_t *regs)
{
    regs->device &= IDE_DEVICE_DEV; // clear all bits except dev
    regs->error &= ~IDE_ERROR_EXEC_DEV_DIAG_DEV1_FAIL; // clear bit 7
    fill_device_signature(regs);
    regs->status &= IDE_STATUS_IDX; // clear bits BSY, 6,5,4,2,0
    ide_phy_set_regs(regs);
    return true;
}

// Set the packet device signature values to PHY registers
// See T13/1410D revision 3a section 9.12 Signature and persistence
bool IDEATAPIDevice::set_device_signature(uint8_t error, bool was_reset)
{
    ide_registers_t regs = {};
    ide_phy_get_regs(&regs);

    regs.error = error;
    fill_device_signature(&regs);

    if (was_reset)
    {
        regs.error = 1; // Diagnostics ok
        regs.status = 0;
    }

    ide_phy_set_regs(&regs);

    if (!was_reset)
    {
        // Command complete
        if (error == IDE_ERROR_ABORT)
            ide_phy_assert_irq(IDE_STATUS_DEVRDY | IDE_STATUS_ERR);
        else 
            ide_phy_assert_irq(IDE_STATUS_DEVRDY);

    }

    return true;
}

// Set the packet device signature values to PHY registers
// See T13/1410D revision 3a section 9.12 Signature and persistence
void IDEATAPIDevice::fill_device_signature(ide_registers_t *regs)
{
    regs->lba_low = 0x01;
    regs->lba_mid = 0x14;
    regs->lba_high = 0xEB;
    regs->sector_count = 0x01;
}

bool IDEATAPIDevice::atapi_send_data(const uint8_t *data, size_t blocksize, size_t num_blocks)
{
    // Report data for command responses
    dbgmsg("---- ATAPI send ", (int)num_blocks, "x", (int)blocksize, " bytes: ",
        bytearray((const uint8_t*)data, blocksize * num_blocks));

    size_t max_blocksize = std::min<size_t>(m_phy_caps.max_blocksize, m_atapi_state.bytes_req);

    for (size_t i = 0; i < num_blocks; i++)
    {
        size_t sent = 0;
        while (sent + max_blocksize < blocksize)
        {
            // Send smaller pieces when max block size exceeded
            if (!atapi_send_data_block(data + blocksize * i + sent, max_blocksize))
            {
                return false;
            }

            sent += max_blocksize;
        }

        // Send rest as single block (common case)
        if (!atapi_send_data_block(data + blocksize * i + sent, blocksize))
        {
            return false;
        }
    }

    return atapi_send_wait_finish();
}

ssize_t IDEATAPIDevice::atapi_send_data_async(const uint8_t *data, size_t blocksize, size_t num_blocks)
{
    if (m_atapi_state.data_state == ATAPI_DATA_WRITE &&
        blocksize == m_atapi_state.blocksize)
    {
        // Fast path, transfer size has already been set up
        size_t blocks_sent = 0;
        while (blocks_sent < num_blocks && ide_phy_can_write_block())
        {
            ide_phy_write_block(data, blocksize);
            data += blocksize;
            blocks_sent++;
        }

        if (blocks_sent == 0)
        {
            if (ide_phy_is_command_interrupted())
            {
                dbgmsg("atapi_send_data_async(): interrupted");
                return -1;
            }
        }

        return blocks_sent;
    }

    size_t max_blocksize = std::min<size_t>(m_phy_caps.max_blocksize, m_atapi_state.bytes_req);
    if (blocksize > max_blocksize)
    {
        dbgmsg("-- atapi_send_data_async(): Block size ", (int)blocksize, " exceeds limit ", (int)max_blocksize,
               ", using atapi_send_data() instead");

        if (atapi_send_data(data, blocksize, num_blocks))
        {
            return num_blocks;
        }
        else
        {
            return -1;
        }
    }
    else
    {
        // Start transmission of first data block
        if (atapi_send_data_block(data, blocksize))
        {
            return 1;
        }
        else
        {
            return -1;
        }
    }
}

bool IDEATAPIDevice::atapi_send_data_is_ready(size_t blocksize)
{
    if (m_atapi_state.data_state != ATAPI_DATA_WRITE
        || blocksize != m_atapi_state.blocksize)
    {
        // Start of transfer or switch of block size
        return ide_phy_is_write_finished();
    }
    else
    {
        // Continuation of transfer
        return ide_phy_can_write_block();
    }
}

bool IDEATAPIDevice::atapi_send_data_block(const uint8_t *data, uint16_t blocksize)
{
    // dbgmsg("---- Send data block ", (uint32_t)data, " ", (int)blocksize, " udma_mode:", m_atapi_state.udma_mode);

    if (m_atapi_state.data_state != ATAPI_DATA_WRITE
        || blocksize != m_atapi_state.blocksize)
    {
        atapi_send_wait_finish();
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
        int udma_mode = (m_atapi_state.dma_requested ? m_atapi_state.udma_mode : -1);
        ide_phy_start_write(blocksize, udma_mode);
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
            if (ide_phy_is_command_interrupted())
            {
                dbgmsg("IDEATAPIDevice::atapi_send_data_block() interrupted");
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

        if (ide_phy_is_command_interrupted())
        {
            dbgmsg("IDEATAPIDevice::atapi_send_wait_finish() interrupted");
            return false;
        }
    }

    // Check for any CRC errors
    ide_phy_stop_transfers(&m_atapi_state.crc_errors);

    return true;
}

bool IDEATAPIDevice::atapi_recv_data(uint8_t *data, size_t blocksize, size_t num_blocks)
{
    size_t phy_max_blocksize = m_phy_caps.max_blocksize;
    size_t max_blocksize = std::min<size_t>(phy_max_blocksize, m_atapi_state.bytes_req);
    if (blocksize > max_blocksize)
    {
        // Have to split the blocks for phy
        size_t split = (blocksize + max_blocksize - 1) / max_blocksize;
        assert(blocksize % split == 0);
        blocksize /= split;
        num_blocks *= split;
    }
    else
    {
        // Combine blocks for better performance
        while (blocksize * 2 < max_blocksize && (num_blocks & 1) == 0)
        {
            blocksize *= 2;
            num_blocks >>= 1;
        }
    }

    // Set number bytes to transfer to registers
    ide_registers_t regs = {};
    ide_phy_get_regs(&regs);
    regs.status = IDE_STATUS_BSY;
    regs.sector_count = 0; // Data transfer to device
    regs.lba_mid = (uint8_t)blocksize;
    regs.lba_high = (uint8_t)(blocksize >> 8);
    ide_phy_set_regs(&regs);

    // Start data transfer for first block
    int udma_mode = (m_atapi_state.dma_requested ? m_atapi_state.udma_mode : -1);
    ide_phy_start_read(blocksize, udma_mode);

    // Receive blocks
    for (size_t i = 0; i < num_blocks; i++)
    {
        uint32_t start = millis();
        while (!ide_phy_can_read_block())
        {
            if ((uint32_t)(millis() - start) > 10000)
            {
                logmsg("IDEATAPIDevice::atapi_recv_data read timeout on block ", (int)(i + 1), "/", (int)num_blocks);
                ide_phy_stop_transfers();
                return false;
            }

            if (ide_phy_is_command_interrupted())
            {
                dbgmsg("IDEATAPIDevice::atapi_recv_data() interrupted");
                return false;
            }
        }

        // Read out previous block
        bool continue_transfer = (i + 1 < num_blocks);
        ide_phy_read_block(data + blocksize * i, blocksize, continue_transfer);
    }

    ide_phy_stop_transfers();
    return true;
}

bool IDEATAPIDevice::atapi_recv_data_block(uint8_t *data, uint16_t blocksize)
{
    // Set number bytes to transfer to registers
    ide_registers_t regs = {};
    ide_phy_get_regs(&regs);
    regs.status = IDE_STATUS_BSY;
    regs.sector_count = 0; // Data transfer to device
    regs.lba_mid = (uint8_t)blocksize;
    regs.lba_high = (uint8_t)(blocksize >> 8);
    ide_phy_set_regs(&regs);

    // Start data transfer
    int udma_mode = (m_atapi_state.dma_requested ? m_atapi_state.udma_mode : -1);
    ide_phy_start_read(blocksize, udma_mode);

    uint32_t start = millis();
    while (!ide_phy_can_read_block())
    {
        if ((uint32_t)(millis() - start) > 10000)
        {
            logmsg("IDEATAPIDevice::atapi_recv_data_block(", (int)blocksize, ") read timeout");
            ide_phy_stop_transfers();
            return false;
        }

        if (ide_phy_is_command_interrupted())
        {
            dbgmsg("IDEATAPIDevice::atapi_recv_data_block() interrupted");
            return false;
        }
    }

    ide_phy_read_block(data, blocksize);
    ide_phy_stop_transfers();
    return true;
}

bool IDEATAPIDevice::handle_atapi_command(const uint8_t *cmd)
{
    // INQUIRY and REQUEST SENSE bypass unit attention
    switch (cmd[0])
    {
        case ATAPI_CMD_INQUIRY:         return atapi_inquiry(cmd);
        case ATAPI_CMD_REQUEST_SENSE:   return atapi_request_sense(cmd);
    }

    if (m_atapi_state.unit_attention)
    {
        m_atapi_state.unit_attention = false;
        return atapi_cmd_error(ATAPI_SENSE_UNIT_ATTENTION, m_atapi_state.sense_asc);
    }

    switch (cmd[0])
    {
        case ATAPI_CMD_TEST_UNIT_READY: return atapi_test_unit_ready(cmd);
        case ATAPI_CMD_START_STOP_UNIT: return atapi_start_stop_unit(cmd);
        case ATAPI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL: return atapi_prevent_allow_removal(cmd);
        case ATAPI_CMD_MODE_SENSE6:     return atapi_mode_sense(cmd);
        case ATAPI_CMD_MODE_SENSE10:    return atapi_mode_sense(cmd);
        case ATAPI_CMD_MODE_SELECT6:    return atapi_mode_select(cmd);
        case ATAPI_CMD_MODE_SELECT10:   return atapi_mode_select(cmd);
        case ATAPI_CMD_GET_CONFIGURATION: return atapi_get_configuration(cmd);
        case ATAPI_CMD_GET_EVENT_STATUS_NOTIFICATION: return atapi_get_event_status_notification(cmd);
        case ATAPI_CMD_READ_CAPACITY:   return atapi_read_capacity(cmd);
        case ATAPI_CMD_READ6:           return atapi_read(cmd);
        case ATAPI_CMD_READ10:          return atapi_read(cmd);
        case ATAPI_CMD_READ12:          return atapi_read(cmd);
        case ATAPI_CMD_WRITE6:          return atapi_write(cmd);
        case ATAPI_CMD_WRITE10:         return atapi_write(cmd);
        case ATAPI_CMD_WRITE12:         return atapi_write(cmd);
        case ATAPI_CMD_WRITE_AND_VERIFY10: return atapi_write(cmd);

        default:
            logmsg("-- WARNING: Unsupported ATAPI command ", get_atapi_command_name(cmd[0]));
            return atapi_cmd_error(ATAPI_SENSE_ILLEGAL_REQ, ATAPI_ASC_INVALID_CMD);
    }
}

static const char *atapi_sense_to_str(uint8_t sense_key)
{
    switch (sense_key)
    {
        case ATAPI_SENSE_NO_SENSE:          return "NO_SENSE";
        case ATAPI_SENSE_RECOVERED:         return "RECOVERED";
        case ATAPI_SENSE_NOT_READY:         return "NOT_READY";
        case ATAPI_SENSE_MEDIUM_ERROR:      return "MEDIUM_ERROR";
        case ATAPI_SENSE_HARDWARE_ERROR:    return "HARDWARE_ERROR";
        case ATAPI_SENSE_ILLEGAL_REQ:       return "ILLEGAL_REQ";
        case ATAPI_SENSE_UNIT_ATTENTION:    return "UNIT_ATTENTION";
        case ATAPI_SENSE_DATA_PROTECT:      return "DATA_PROTECT";
        case ATAPI_SENSE_ABORTED_CMD:       return "ABORTED_CMD";
        case ATAPI_SENSE_MISCOMPARE:        return "MISCOMPARE";
        default:                            return "UNKNOWN_SENSE";
    }
}

bool IDEATAPIDevice::atapi_cmd_not_ready_error()
{
    return atapi_cmd_error(ATAPI_SENSE_NOT_READY, ATAPI_ASC_NO_MEDIUM);
}

bool IDEATAPIDevice::atapi_cmd_error(uint8_t sense_key, uint16_t sense_asc)
{
    if (sense_key == ATAPI_SENSE_UNIT_ATTENTION)
    {
        dbgmsg("-- Reporting UNIT_ATTENTION condition after reset/medium change (ASC:", sense_asc, ")");
    }
    else
    {
        dbgmsg("-- ATAPI error: ", sense_key, " ", sense_asc, " (", atapi_sense_to_str(sense_key), ")");
    }

    m_atapi_state.sense_key = sense_key;
    m_atapi_state.sense_asc = sense_asc;
    m_atapi_state.data_state = ATAPI_DATA_IDLE;

    ide_registers_t regs = {};
    ide_phy_get_regs(&regs);
    regs.error = IDE_ERROR_ABORT | (sense_key << 4);
    regs.sector_count = ATAPI_SCOUNT_IS_CMD | ATAPI_SCOUNT_TO_HOST;
    regs.lba_mid = 0xFE;
    regs.lba_high = 0xFF;
    ide_phy_set_regs(&regs);
    ide_phy_assert_irq(IDE_STATUS_DEVRDY | IDE_STATUS_ERR);

    return true;
}

bool IDEATAPIDevice::atapi_cmd_ok()
{
    if (m_atapi_state.crc_errors > 0)
    {
        logmsg("-- Detected ", m_atapi_state.crc_errors, " CRC errors during transfer, reporting error to host");
        return atapi_cmd_error(ATAPI_SENSE_HARDWARE_ERROR, ATAPI_ASC_CRC_ERROR);
    }

    dbgmsg("-- ATAPI success");
    m_atapi_state.sense_key = 0;
    m_atapi_state.sense_asc = 0;
    m_atapi_state.data_state = ATAPI_DATA_IDLE;

    ide_registers_t regs = {};
    ide_phy_get_regs(&regs);
    regs.error = 0;
    regs.sector_count = ATAPI_SCOUNT_IS_CMD | ATAPI_SCOUNT_TO_HOST;
    regs.lba_mid = 0xFE;
    regs.lba_high = 0xFF;
    ide_phy_set_regs(&regs);
    ide_phy_assert_irq(IDE_STATUS_DEVRDY);

    return true;
}

bool IDEATAPIDevice::atapi_test_unit_ready(const uint8_t *cmd)
{
    if (!has_image())
    {
        return atapi_cmd_not_ready_error();
    }

    if (m_devinfo.removable && m_removable.ejected)
    {
        if (m_removable.reinsert_media_after_eject)
        {
            insert_media();
        }
        return atapi_cmd_not_ready_error();
    }

    if (m_atapi_state.not_ready)
    {
        m_atapi_state.not_ready = false;
        return atapi_cmd_not_ready_error();
    }
    return atapi_cmd_ok();
}

bool IDEATAPIDevice::atapi_start_stop_unit(const uint8_t *cmd)
{
    uint8_t cmd_eject = *(cmd + ATAPI_START_STOP_EJT_OFFSET);
    if ((ATAPI_START_STOP_PWR_CON_MASK & cmd_eject) == 0 &&
        (ATAPI_START_STOP_LOEJ & cmd_eject) != 0)
    {
        // Eject condition
        if ((ATAPI_START_STOP_START & cmd_eject) == 0)
        {
            if (m_removable.prevent_removable)
            {
                if (is_medium_present())
                    return atapi_cmd_error(ATAPI_SENSE_ILLEGAL_REQ, ATAPI_ASC_MEDIUM_REMOVAL_PREVENTED);
                else
                    return atapi_cmd_error(ATAPI_SENSE_NOT_READY, ATAPI_ASC_MEDIUM_REMOVAL_PREVENTED);
            }
            else
                eject_media();
        }
        // Load condition
        else
        {
            insert_media();
        }
    }
    return atapi_cmd_ok();

}


bool IDEATAPIDevice::atapi_prevent_allow_removal(const uint8_t *cmd)
{
    if (m_removable.ignore_prevent_removal)
    {
        dbgmsg("-- Ignoring host request to change prevent removable via ini file setting");
    }
    else
    {
        m_removable.prevent_removable = cmd[4] & 1;
        m_removable.prevent_persistent = cmd[4] & 2;

        // We can't actually prevent SD card from being removed
        dbgmsg("-- Host requested prevent=", (int)m_removable.prevent_removable, " persistent=", (int)m_removable.prevent_persistent);
    }
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
    strncpy((char*)&inquiry[ATAPI_INQUIRY_VENDOR], m_devinfo.atapi_vendor, 8);
    strncpy((char*)&inquiry[ATAPI_INQUIRY_PRODUCT], m_devinfo.atapi_product, 16);
    strncpy((char*)&inquiry[ATAPI_INQUIRY_REVISION], m_devinfo.atapi_version, 4);

    if (req_bytes < count) count = req_bytes;
    atapi_send_data(inquiry, count);

    if (m_removable.reinsert_media_on_inquiry)
    {
        insert_media();
    }

    return atapi_cmd_ok();
}

bool IDEATAPIDevice::atapi_mode_sense(const uint8_t *cmd)
{
    uint8_t page_ctrl, page_idx;
    uint16_t req_bytes;
    if (cmd[0] == ATAPI_CMD_MODE_SENSE6)
    {
        page_ctrl = cmd[2] >> 6;
        page_idx = cmd[2] & 0x3F;
        req_bytes = cmd[4];
    }
    else if (cmd[0] == ATAPI_CMD_MODE_SENSE10)
    {
        page_ctrl = cmd[2] >> 6;
        page_idx = cmd[2] & 0x3F;
        req_bytes = parse_be16(&cmd[7]);
    }
    else
    {
        assert(false);
    }

    uint8_t *resp = m_buffer.bytes + 8; // Reserve space for mode parameter header
    size_t max_bytes = sizeof(m_buffer) - 8;
    size_t resp_bytes = 0;

    if (page_idx != 0x3F)
    {
        // Request single page
        resp_bytes += atapi_get_mode_page(page_ctrl, page_idx, &resp[resp_bytes], max_bytes - resp_bytes);
        dbgmsg("-- Request page ", page_idx, ", response length ", (int)resp_bytes);
    }
    else
    {
        // Request all pages
        for (int i = 0x01; i < 0x3F && resp_bytes < max_bytes; i++)
        {
            resp_bytes += atapi_get_mode_page(page_ctrl, i, &resp[resp_bytes], max_bytes - resp_bytes);
        }
        resp_bytes += atapi_get_mode_page(page_ctrl, 0, &resp[resp_bytes], max_bytes - resp_bytes);
        dbgmsg("-- Request all pages, response length ", (int)resp_bytes);
    }

    // Fill in mode parameter header
    resp_bytes += 8;
    uint8_t *hdr = m_buffer.bytes;
    memset(hdr, 0, 8);
    write_be16(&hdr[0], resp_bytes - 2);
    hdr[2] = m_devinfo.medium_type;

    if (resp_bytes > req_bytes) resp_bytes = req_bytes;
    atapi_send_data(m_buffer.bytes, resp_bytes);

    return atapi_cmd_ok();
}

bool IDEATAPIDevice::atapi_mode_select(const uint8_t *cmd)
{
    bool save_pages;
    uint16_t paramLength;

    if (cmd[0] == ATAPI_CMD_MODE_SELECT6)
    {
        save_pages = cmd[1] & 1;
        paramLength = cmd[4];
    }
    else if (cmd[0] == ATAPI_CMD_MODE_SELECT10)
    {
        save_pages = cmd[1] & 1;
        paramLength = parse_be16(&cmd[7]);
    }
    else
    {
        assert(false);
    }

    dbgmsg("-- MODE SELECT, save pages: ", (int)save_pages, ", paramLength ", (int)paramLength);

    uint8_t *buf = m_buffer.bytes;
    if (paramLength > sizeof(m_buffer))
    {
        return atapi_cmd_error(ATAPI_SENSE_ILLEGAL_REQ, ATAPI_ASC_PARAMETER_LENGTH_ERROR);
    }

    if (!atapi_recv_data_block(buf, paramLength))
    {
        dbgmsg("-- Failed to read parameter list");
        return atapi_cmd_error(ATAPI_SENSE_ABORTED_CMD, 0);
    }

    while (buf < &m_buffer.bytes[paramLength])
    {
        uint8_t page_idx = buf[0] & 0x3F;
        uint8_t page_ctrl = buf[0] >> 6;
        uint16_t datalength = buf[1] + 2;

        dbgmsg("-- Set mode page ", page_idx, ", value ", bytearray(buf, datalength));
        atapi_set_mode_page(page_ctrl, page_idx, buf, datalength);
        buf += datalength;
    }

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

    m_atapi_state.unit_attention = false;

    return atapi_cmd_ok();
}

bool IDEATAPIDevice::atapi_get_configuration(const uint8_t *cmd)
{
    uint8_t rt = cmd[1] & 3;
    uint16_t starting_feature = parse_be16(&cmd[2]);
    uint16_t req_bytes = parse_be16(&cmd[7]);

    uint8_t *resp = m_buffer.bytes;
    size_t max_bytes = sizeof(m_buffer);
    size_t resp_bytes = 8; // Reserve space for feature header

    if (rt == ATAPI_RT_ALL)
    {
        // All supported features
        for (uint16_t i = starting_feature; i <= ATAPI_FEATURE_MAX && resp_bytes < max_bytes; i++)
        {
            resp_bytes += atapi_get_configuration(rt, i, &resp[resp_bytes], max_bytes - resp_bytes);
        }
    }
    else if (rt == ATAPI_RT_ALL_CURRENT)
    {
        // Only current features
        for (uint16_t i = starting_feature; i <= ATAPI_FEATURE_MAX && resp_bytes < max_bytes; i++)
        {
            size_t len = atapi_get_configuration(rt, i, &resp[resp_bytes], max_bytes - resp_bytes);
            if (len > 0 && (resp[resp_bytes + 2] & 1))
            {
                resp_bytes += len;
            }
        }
    }
    else if (rt == ATAPI_RT_SINGLE)
    {
        // Single feature
        resp_bytes += atapi_get_configuration(rt, starting_feature, &resp[resp_bytes], max_bytes - resp_bytes);
    }

    // Fill in feature header
    write_be32(resp, resp_bytes - 4);
    resp[4] = 0;
    resp[5] = 0;
    if (is_medium_present())
        write_be16(&resp[6], m_devinfo.current_profile);
    else
        write_be16(&resp[6], 0);

    if (resp_bytes > req_bytes) resp_bytes = req_bytes;
    atapi_send_data(m_buffer.bytes, resp_bytes);

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
    if (!is_medium_present()) return atapi_cmd_not_ready_error();

    if (m_atapi_state.not_ready) return atapi_cmd_error(ATAPI_SENSE_NOT_READY, ATAPI_ASC_UNIT_BECOMING_READY);

    uint32_t last_lba = this->capacity_lba() - 1;
    uint8_t *buf = m_buffer.bytes;
    write_be32(&buf[0], last_lba);
    write_be32(&buf[4], m_devinfo.bytes_per_sector);
    atapi_send_data(buf, 8);

    return atapi_cmd_ok();
}

// Parse ATAPI READ commands
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

    if (!is_medium_present()) return atapi_cmd_not_ready_error();
    if (m_atapi_state.not_ready) return atapi_cmd_error(ATAPI_SENSE_NOT_READY, ATAPI_ASC_UNIT_BECOMING_READY);

    if (lba + transfer_len > capacity_lba())
    {
        logmsg("-- Host attempted read at LBA ", (int)lba, "+", (int)transfer_len,
            ", beyond capacity ", capacity_lba());
        return atapi_cmd_error(ATAPI_SENSE_ILLEGAL_REQ, ATAPI_ASC_LBA_OUT_OF_RANGE);
    }

    dbgmsg("-- Read ", (int)transfer_len, " sectors starting at ", (int)lba);
    return doRead(lba, transfer_len);
}

// Start actual read transfer from image file (can be called directly by subclasses)
bool IDEATAPIDevice::doRead(uint32_t lba, uint32_t transfer_len)
{
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

// IDEImage implementation calls this when new data is available from file.
// This will send the data to IDE bus.
ssize_t IDEATAPIDevice::read_callback(const uint8_t *data, size_t blocksize, size_t num_blocks)
{
    platform_poll();
    return atapi_send_data_async(data, blocksize, num_blocks);
}

// Parse ATAPI WRITE command
bool IDEATAPIDevice::atapi_write(const uint8_t *cmd)
{
    if (!m_devinfo.writable || (m_image && !m_image->writable()))
    {
        return atapi_cmd_error(ATAPI_SENSE_ABORTED_CMD, ATAPI_ASC_WRITE_PROTECTED);
    }
    else if (!is_medium_present())
    {
        return atapi_cmd_not_ready_error();
    }
    else if (m_atapi_state.not_ready) return atapi_cmd_error(ATAPI_SENSE_NOT_READY, ATAPI_ASC_UNIT_BECOMING_READY);

    uint32_t lba, transfer_len;
    if (cmd[0] == ATAPI_CMD_WRITE6)
    {
        lba = parse_be24(&cmd[1]) & 0x1FFFFF;
        transfer_len = cmd[4];
        if (transfer_len == 0) transfer_len = 256;
    }
    else if (cmd[0] == ATAPI_CMD_WRITE10 || cmd[0] == ATAPI_CMD_WRITE_AND_VERIFY10)
    {
        lba = parse_be32(&cmd[2]);
        transfer_len = parse_be16(&cmd[7]);
    }
    else if (cmd[0] == ATAPI_CMD_WRITE12)
    {
        lba = parse_be32(&cmd[2]);
        transfer_len = parse_be32(&cmd[6]);
    }
    else
    {
        assert(false);
    }

    if (lba + transfer_len > capacity_lba())
    {
        logmsg("-- Host attempted write at LBA ", (int)lba, "+", (int)transfer_len,
            ", beyond capacity ", (int)capacity_lba());
        return atapi_cmd_error(ATAPI_SENSE_ILLEGAL_REQ, ATAPI_ASC_LBA_OUT_OF_RANGE);
    }

    dbgmsg("-- Write ", (int)transfer_len, " sectors starting at ", (int)lba);
    return doWrite(lba, transfer_len);
}

// Start write transfer to image file. Can be called directly by subclasses.
bool IDEATAPIDevice::doWrite(uint32_t lba, uint32_t transfer_len)
{
    bool status = m_image->write((uint64_t)lba * m_devinfo.bytes_per_sector,
                                m_devinfo.bytes_per_sector, transfer_len,
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

// Called by IDEImage to request reception of more data from IDE bus
ssize_t IDEATAPIDevice::write_callback(uint8_t *data, size_t blocksize, size_t num_blocks, bool first_xfer, bool last_xfer)
{
    if (atapi_recv_data(data, blocksize, num_blocks))
    {
        return num_blocks;
    }
    else
    {
        logmsg("IDEATAPIDevice::write_callback(", (int)blocksize, ", ", (int)num_blocks, ") failed");
        return -1;
    }
}

size_t IDEATAPIDevice::atapi_get_mode_page(uint8_t page_ctrl, uint8_t page_idx, uint8_t *buffer, size_t max_bytes)
{
    return 0;
}

void IDEATAPIDevice::atapi_set_mode_page(uint8_t page_ctrl, uint8_t page_idx, const uint8_t *buffer, size_t length)
{

}

size_t IDEATAPIDevice::atapi_get_configuration(uint8_t return_type, uint16_t feature, uint8_t *buffer, size_t max_bytes)
{
    if (feature == ATAPI_FEATURE_PROFILES)
    {
        // List of profiles supported by device
        write_be16(&buffer[0], feature);
        buffer[2] = 0x03; // Version, Persistent, Current
        buffer[3] = m_devinfo.num_profiles * 4;

        for (int i = 0; i < m_devinfo.num_profiles; i++)
        {
            write_be16(&buffer[4 + i * 4], m_devinfo.profiles[i]);
            buffer[2] = is_medium_present() ? 1 : 0;
            buffer[3] = 0;
        }

        return 4 + m_devinfo.num_profiles * 4;
    }

    if (feature == ATAPI_FEATURE_CORE)
    {
        write_be16(&buffer[0], feature);
        buffer[2] = 0x07;
        buffer[3] = 8;
        write_be32(&buffer[4], 2); // ATAPI standard
        buffer[8] = 0; // DBE not supported
        buffer[9] = 0;
        buffer[10] = 0;
        buffer[11] = 0;
        return 12;
    }

    return 0;
}

bool IDEATAPIDevice::is_medium_present()
{
    return has_image() && (!m_devinfo.removable || (m_devinfo.removable && !m_removable.ejected));
}

void IDEATAPIDevice::eject_button_poll(bool immediate)
{
    // treat '1' to '0' transitions as eject actions
    static uint8_t previous = 0x00;
    uint8_t bitmask = platform_get_buttons();
    uint8_t ejectors = (previous ^ bitmask) & previous;
    previous = bitmask;

    // defer ejection until the bus is idle
    static uint8_t deferred = 0x00;
    if (!immediate)
    {
        deferred |= ejectors;
        return;
    }
    else
    {
        ejectors |= deferred;
        deferred = 0;

        if (ejectors)
        {
            //m_atapi_state.unit_attention = true;
            dbgmsg("Ejection button pressed");
            if (m_removable.ejected)
                insert_media();
            else
            {
                button_eject_media();
            }
        }
        return;
    }
}

void IDEATAPIDevice::button_eject_media()
{
    if (!m_removable.prevent_removable)
        eject_media();
}

void IDEATAPIDevice::eject_media()
{
    char filename[MAX_FILE_PATH+1];
    m_image->get_filename(filename, sizeof(filename));
    logmsg("Device ejecting media: \"", filename, "\"");
    m_removable.ejected = true;
}

void IDEATAPIDevice::insert_media(IDEImage *image)
{
    zuluide::images::ImageIterator img_iterator;
    char filename[MAX_FILE_PATH+1];

    if (m_devinfo.removable)
    {
        if (image != nullptr)
        {
            set_image(image);
            m_removable.ejected = false;
            m_atapi_state.not_ready = true;
        }
        else if (m_removable.ejected)
        {
            img_iterator.Reset();
            if (!img_iterator.IsEmpty())
            {
                if (m_image)
                {
                    m_image->get_filename(filename, sizeof(filename));
                    if (!img_iterator.MoveToFile(filename))
                    {
                        img_iterator.MoveNext();
                    }
                }
                else
                {
                    img_iterator.MoveNext();
                }

                if (img_iterator.IsLast())
                {
                    img_iterator.MoveFirst();
                }
                else
                {
                    img_iterator.MoveNext();
                }
                g_ide_imagefile.clear();
                if (g_ide_imagefile.open_file(img_iterator.Get().GetFilename().c_str(), false))
                {
                    set_image(&g_ide_imagefile);
                    logmsg("-- Device loading media: \"", img_iterator.Get().GetFilename().c_str(), "\"");
                    m_removable.ejected = false;
                    m_atapi_state.not_ready = true;
                }
            }
            img_iterator.Cleanup();
        }
    }
}

void IDEATAPIDevice::sd_card_inserted()
{
    if (m_devinfo.removable
        && m_removable.reinsert_media_after_sd_insert
        && m_removable.ejected)
    {
        insert_media();
    }
}

void IDEATAPIDevice::set_inquiry_strings(const char* default_vendor, const char* default_product, const char* default_version)
{
    char input_str[17];
    uint8_t input_len;

    memset(input_str, ' ', 16);
    input_len = ini_gets("IDE", "atapi_product", default_product, input_str, 17, CONFIGFILE);
    memcpy(m_devinfo.atapi_product, input_str, input_len);

    memset(input_str, ' ', 8);
    input_len = ini_gets("IDE","atapi_vendor", default_vendor, input_str, 9, CONFIGFILE);
    memcpy(m_devinfo.atapi_vendor, input_str, input_len);

    memset(input_str, ' ', 4);
    input_len = ini_gets("IDE","atapi_version", default_version, input_str, 5, CONFIGFILE);
    memcpy(m_devinfo.atapi_version, input_str, input_len);
}
