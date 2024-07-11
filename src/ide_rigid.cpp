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

#include "ide_rigid.h"
#include "ide_utils.h"
#include "atapi_constants.h"
#include "ZuluIDE.h"
#include "ZuluIDE_config.h"

extern uint8_t g_ide_signals;
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

void IDERigidDevice::initialize(int devidx)
{
    memset(&m_devinfo, 0, sizeof(m_devinfo));
    memset(&m_ata_state, 0, sizeof(m_ata_state));
    memset(&m_removable, 0, sizeof(m_removable));
    strncpy(m_devinfo.serial_number, "123456789", sizeof(m_devinfo.serial_number));
    strncpy(m_devinfo.model_number, "ZuluIDE Hard Drive", sizeof(m_devinfo.model_number));
    strncpy(m_devinfo.firmware_rev, "1.0", sizeof(m_devinfo.firmware_rev));
    m_devinfo.bytes_per_sector = 512;
    m_devinfo.writable = true;
    IDEDevice::initialize(devidx);
}

void IDERigidDevice::reset()
{
    memset(&m_removable, 0, sizeof(m_removable));
}

void IDERigidDevice::set_image(IDEImage *image)
{
    m_image = image;
    // \todo replace with ATA media change
    // m_atapi_state.unit_attention = true;
    // m_atapi_state.sense_asc = ATAPI_ASC_MEDIUM_CHANGE;
}

bool IDERigidDevice::handle_command(ide_registers_t *regs)
{

    switch (regs->command)
    {
        // Command need the device signature
        case IDE_CMD_DEVICE_RESET:
//        case IDE_CMD_EXECUTE_DEVICE_DIAGNOSTIC:
            return set_device_signature(IDE_ERROR_ABORT, false);

        // Supported IDE commands
        case IDE_CMD_NOP: return cmd_nop(regs);
        case IDE_CMD_SET_FEATURES: return cmd_set_features(regs);
        case IDE_CMD_READ_DMA: return cmd_read(regs, true);
        case IDE_CMD_WRITE_DMA: return cmd_write(regs, true);
        case IDE_CMD_READ_SECTORS: return cmd_read(regs, false);
        case IDE_CMD_WRITE_SECTORS: return cmd_write(regs, false);
        case IDE_CMD_INIT_DEV_PARAMS: return cmd_init_dev_params(regs);
        case IDE_CMD_IDENTIFY_DEVICE: return cmd_identify_device(regs);
        case IDE_CMD_RECALIBRATE: return cmd_recalibrate(regs);


        default: return false;
    }
}

static bool is_lba_mode(ide_registers_t *regs)
{
    return regs->device & IDE_DEVICE_LBA;
}

bool IDERigidDevice::cmd_nop(ide_registers_t *regs)
{
    // CMD_NOP always fails with CMD_ABORTED
    regs->error = IDE_ERROR_ABORT;
    ide_phy_set_regs(regs);
    ide_phy_assert_irq(IDE_STATUS_DEVRDY | IDE_STATUS_DSC | IDE_STATUS_ERR);
    return true;
}

// Set configuration based on register contents
bool IDERigidDevice::cmd_set_features(ide_registers_t *regs)
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
            m_ata_state.udma_mode = -1;
            dbgmsg("-- Set PIO default transfer mode");
        }
        else if (mode_major == 1 && mode_minor <= m_phy_caps.max_pio_mode)
        {
            m_ata_state.udma_mode = -1;
            dbgmsg("-- Set PIO transfer mode ", (int)mode_minor);
        }
        else if (mode_major == 8 && mode_minor <= m_phy_caps.max_udma_mode)
        {
            m_ata_state.udma_mode = mode_minor;
            dbgmsg("-- Set UDMA transfer mode ", (int)mode_minor);
        }
        else
        {
            dbgmsg("-- Unsupported mode ", mode, " (major ", (int)mode_major, " minor ", (int)mode_minor, ")");
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
        ide_phy_assert_irq(IDE_STATUS_DEVRDY | IDE_STATUS_DSC);
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

bool IDERigidDevice::cmd_read(ide_registers_t *regs, bool dma_transfer)
{
    if (dma_transfer && m_phy_caps.max_udma_mode < 0)
        return false;

    bool lba_mode = false;
    uint32_t lba = 0;
    uint16_t sector_count = regs->sector_count == 0 ? 256 : regs->sector_count;
    uint8_t head = 0;
    uint16_t cylinder = 0;
    uint8_t sector = 0;
    m_ata_state.data_state = ATA_DATA_IDLE;
    m_ata_state.dma_requested = dma_transfer;
    m_ata_state.crc_errors = 0;


    lba_mode = !!(regs->device & 0x40);
    if (lba_mode)
    {
        lba |= 0xF & (regs->device << 24);
        lba |= regs->lba_high << 16;
        lba |= regs->lba_mid << 8;
        lba |= regs->lba_low;
    }
    else
    {
        head = 0xF & (regs->device);
        cylinder = regs->lba_high << 8;
        cylinder |= regs->lba_mid;
        sector = regs->lba_low;
        lba = (cylinder * m_devinfo.heads + head) * m_devinfo.sectors_per_track + (sector - 1);
    }
    bool status = m_image->read((uint64_t)lba * m_devinfo.bytes_per_sector, m_devinfo.bytes_per_sector, sector_count, this);
    status = status && ata_send_wait_finish();
    if (status)
    {
        uint32_t new_lba = lba + sector_count - 1;
        ide_phy_get_regs(regs);
        if (lba_mode)
        {
            regs->device &= 0xF0;
            regs->device |= (0x0F) & (new_lba >> 24);
            regs->lba_high = new_lba >> 16;
            regs->lba_mid = new_lba >> 8;
            regs->lba_low = new_lba;
        }
        else
        {
            sector = (new_lba % m_devinfo.sectors_per_track) + 1;
            cylinder = (new_lba / m_devinfo.sectors_per_track) / m_devinfo.heads;
            head = (new_lba / m_devinfo.sectors_per_track) % m_devinfo.heads;
            regs->device &= 0xF0; 
            regs->device |= (0x0F) & head;
            regs->lba_high = cylinder >> 8;
            regs->lba_mid = cylinder;
            regs->lba_low = sector;
        }
        m_ata_state.data_state = ATA_DATA_IDLE;
        regs->status = IDE_STATUS_DEVRDY | IDE_STATUS_DSC;
        ide_phy_set_regs(regs);
        ide_phy_assert_irq(IDE_STATUS_DEVRDY | IDE_STATUS_DSC);
    }
    return status;
}

bool IDERigidDevice::cmd_write(ide_registers_t *regs, bool dma_transfer)
{
    if (dma_transfer && m_phy_caps.max_udma_mode < 0)
        return false;
    bool lba_mode = false;
    uint32_t lba = 0;
    uint16_t sector_count = regs->sector_count == 0 ? 256 : regs->sector_count;
    uint8_t head = 0;
    uint16_t cylinder = 0;
    uint8_t sector = 0;
    bool status = false;
    m_ata_state.data_state = ATA_DATA_IDLE;
    m_ata_state.dma_requested = dma_transfer;
    m_ata_state.crc_errors = 0;

    lba_mode = !!(regs->device & 0x40);
    if (lba_mode)
    {
        lba |= 0xF & (regs->device) << 24;
        lba |= regs->lba_high << 16;
        lba |= regs->lba_mid << 8;
        lba |= regs->lba_low;
    }
    else
    {
        head = 0xF & (regs->device);
        cylinder = regs->lba_high << 8;
        cylinder |= regs->lba_mid;
        sector = regs->lba_low;
        lba = (cylinder * m_devinfo.heads + head) * m_devinfo.sectors_per_track + (sector - 1);
    }

    if (m_image && m_image->writable())
    {
    status = m_image->write((uint64_t)lba * m_devinfo.bytes_per_sector,
                            m_devinfo.bytes_per_sector, sector_count,
                            this);
    }

    if (status)
    {
        ide_phy_get_regs(regs);
        uint32_t new_lba = lba + sector_count - 1;
        if (lba_mode)
        {
            regs->device |= (0x0F) & (new_lba >> 24);
            regs->lba_high = new_lba >> 16;
            regs->lba_mid = new_lba >> 8;
            regs->lba_low = new_lba;
        }
        else
        {
            sector = (new_lba % m_devinfo.sectors_per_track) + 1;
            cylinder = (new_lba / m_devinfo.sectors_per_track) / m_devinfo.heads;
            head = (new_lba / m_devinfo.sectors_per_track) % m_devinfo.heads;
            regs->device |= (0x0F) & head;
            regs->lba_high = cylinder >> 8;
            regs->lba_mid = cylinder;
            regs->lba_low = sector;

        }
        regs->status = IDE_STATUS_DEVRDY;
        ide_phy_set_regs(regs);
        ide_phy_assert_irq(IDE_STATUS_DEVRDY);
    }
    else
    {
        // \todo do something if the read fails
    }
    return status;
}

bool IDERigidDevice::cmd_init_dev_params(ide_registers_t *regs)
{
    m_devinfo.sectors_per_track =  regs->sector_count;
    m_devinfo.heads = (0x0F & regs->device) + 1;
    dbgmsg("Setting initial dev parameters: sectors/track = ", (int) m_devinfo.sectors_per_track, ", heads = ", (int)m_devinfo.heads);

    regs->status = IDE_STATUS_DEVRDY | IDE_STATUS_DSC;
    regs->error = 0;
    ide_phy_set_regs(regs);
    // Command complete
    ide_phy_assert_irq(IDE_STATUS_DEVRDY | IDE_STATUS_DSC);
    return true;
}

// Responds with 512 bytes of identification data
bool IDERigidDevice::cmd_identify_device(ide_registers_t *regs)
{
    uint16_t idf[256] = {0};
    // Apple IDE hard drive settings - model DSAA-3360
    // idf[IDE_IDENTIFY_OFFSET_GENERAL_CONFIGURATION] = 0x045A;
    // idf[IDE_IDENTIFY_OFFSET_NUM_CYLINDERS] = 0x03A1;
    // idf[IDE_IDENTIFY_OFFSET_NUM_HEADS] = 0x0010;
    // idf[IDE_IDENTIFY_OFFSET_BYTES_PER_TRACK] = 0xE808;
    // idf[IDE_IDENTIFY_OFFSET_BYTES_PER_SECTOR] = 0x0226;
    // idf[IDE_IDENTIFY_OFFSET_SECTORS_PER_TRACK] = 0x0030;
    // copy_id_string(&idf[IDE_IDENTIFY_OFFSET_SERIAL_NUMBER], 10, "PA9P8097455");
    // idf[IDE_IDENTIFY_OFFSET_BUFFER_TYPE] = 0x0003;
    // idf[IDE_IDENTIFY_OFFSET_BUFFER_SIZE_512] = 0x00C0;
    // idf[IDE_IDENTIFY_OFFSET_ECC_LONG_CMDS] = 0x0010;
    // copy_id_string(&idf[IDE_IDENTIFY_OFFSET_FIRMWARE_REV], 4, "3D1A6QA4");
    // copy_id_string(&idf[IDE_IDENTIFY_OFFSET_MODEL_NUMBER], 20, "DSAA-3360");
    // idf[IDE_IDENTIFY_OFFSET_MAX_SECTORS] = 0x0020;
    // idf[IDE_IDENTIFY_OFFSET_CAPABILITIES_1] = 0x0F00;
    // idf[IDE_IDENTIFY_OFFSET_PIO_MODE_ATA1] = 0x0200;
    // idf[IDE_IDENTIFY_OFFSET_OLD_DMA_TIMING_MODE] = 0x0200;
    // idf[IDE_IDENTIFY_OFFSET_MODE_INFO_VALID] = 0x0003;
    // idf[IDE_IDENTIFY_OFFSET_CURRENT_CYLINDERS] = 0x03A1;
    // idf[IDE_IDENTIFY_OFFSET_CURRENT_HEADS] = 0x0010;
    // idf[IDE_IDENTIFY_OFFSET_CURRENT_SECTORS_PER_TRACK] = 0x0030;
    // idf[IDE_IDENTIFY_OFFSET_CURRENT_CAPACITY_IN_SECTORS_LOW] = 0xE300;
    // idf[IDE_IDENTIFY_OFFSET_CURRENT_CAPACITY_IN_SECTORS_HI] = 0x000A;
    // idf[IDE_IDENTIFY_OFFSET_MULTI_SECTOR_VALID] = 0x0120;
    // idf[IDE_IDENTIFY_OFFSET_TOTAL_SECTORS]     = 0xE300;
    // idf[IDE_IDENTIFY_OFFSET_TOTAL_SECTORS + 1] = 0x000A;
    // idf[62] = 0;// 0x0007; // disabling single word dma
    // idf[IDE_IDENTIFY_OFFSET_MODEINFO_MULTIWORD] = 0; // 0x0103; // disabling multi-word dma
    // idf[IDE_IDENTIFY_OFFSET_MODEINFO_PIO] = 0x0001;
    // idf[IDE_IDENTIFY_OFFSET_MULTIWORD_CYCLETIME_MIN] = 0x00F0;
    // idf[IDE_IDENTIFY_OFFSET_MULTIWORD_CYCLETIME_REC] = 0x00F0;
    // idf[IDE_IDENTIFY_OFFSET_PIO_CYCLETIME_MIN] = 0x00F0;
    // idf[IDE_IDENTIFY_OFFSET_PIO_CYCLETIME_IORDY] = 0x00B4; 
    // idf[129] = 0x000B;

     // Generic IDE hard drive
    idf[IDE_IDENTIFY_OFFSET_GENERAL_CONFIGURATION] = (m_devinfo.removable ? 0x80 : 0x40); // Device type
    // \todo Calc these from LBA or maybe visa versa
    idf[IDE_IDENTIFY_OFFSET_NUM_CYLINDERS] = 0x03A1;
    idf[IDE_IDENTIFY_OFFSET_NUM_HEADS] = 0x0010;
    idf[IDE_IDENTIFY_OFFSET_BYTES_PER_TRACK] = 0xE808;
    idf[IDE_IDENTIFY_OFFSET_BYTES_PER_SECTOR] = 0x0226;
    idf[IDE_IDENTIFY_OFFSET_SECTORS_PER_TRACK] = 0x0030;
    copy_id_string(&idf[IDE_IDENTIFY_OFFSET_SERIAL_NUMBER], 10, m_devinfo.serial_number);
    // \todo set on older drives
    // idf[IDE_IDENTIFY_OFFSET_BUFFER_TYPE] = 0x0003;
    // idf[IDE_IDENTIFY_OFFSET_BUFFER_SIZE_512] = 0x00C0;
    // idf[IDE_IDENTIFY_OFFSET_ECC_LONG_CMDS] = 0x0010;
    copy_id_string(&idf[IDE_IDENTIFY_OFFSET_FIRMWARE_REV], 4, m_devinfo.firmware_rev);
    copy_id_string(&idf[IDE_IDENTIFY_OFFSET_MODEL_NUMBER], 20, m_devinfo.model_number);
    idf[IDE_IDENTIFY_OFFSET_MAX_SECTORS] = 0x8020;
    
    idf[IDE_IDENTIFY_OFFSET_CAPABILITIES_1] = (m_phy_caps.supports_iordy ? 1 << 11 : 0) | 
                                            // 1 << 10 | 
                                            (1 << 9) | 
                                            (m_phy_caps.max_udma_mode >= 0 ? 1 << 8 : 0); //IORDY may be support or disabled and LBA supported
    idf[IDE_IDENTIFY_OFFSET_PIO_MODE_ATA1] = (m_phy_caps.max_pio_mode << 8);
    // \todo set on older drives
    // idf[IDE_IDENTIFY_OFFSET_OLD_DMA_TIMING_MODE] = 0x0200;
    idf[IDE_IDENTIFY_OFFSET_MODE_INFO_VALID] |= 0x04; // UDMA support word valid
    idf[IDE_IDENTIFY_OFFSET_MODE_INFO_VALID] |= 0x02; // PIO support word valid
    // \todo set on older drives
    // idf[IDE_IDENTIFY_OFFSET_CURRENT_CYLINDERS] = 0x03A1;
    // idf[IDE_IDENTIFY_OFFSET_CURRENT_HEADS] = 0x0010;
    // idf[IDE_IDENTIFY_OFFSET_CURRENT_SECTORS_PER_TRACK] = 0x0030;
    uint64_t sectors = capacity_lba();
    idf[IDE_IDENTIFY_OFFSET_CURRENT_CAPACITY_IN_SECTORS_LOW] = sectors & 0xFFFF;;
    idf[IDE_IDENTIFY_OFFSET_CURRENT_CAPACITY_IN_SECTORS_HI] = (sectors >> 16) & 0xFFFF;
    idf[IDE_IDENTIFY_OFFSET_TOTAL_SECTORS]     = sectors & 0xFFFF;
    idf[IDE_IDENTIFY_OFFSET_TOTAL_SECTORS + 1] = (sectors >> 16) & 0xFFFF;
    idf[IDE_IDENTIFY_OFFSET_MODEINFO_SINGLEWORD] = 0;// 0x0007; // disabling single word dma
    idf[IDE_IDENTIFY_OFFSET_MODEINFO_MULTIWORD] = 0; // 0x0103; // disabling multi-word dma
    
    idf[IDE_IDENTIFY_OFFSET_MODEINFO_PIO] = (m_phy_caps.max_pio_mode >= 3) ? 1 : 0; // PIO3 supported?
    idf[IDE_IDENTIFY_OFFSET_PIO_CYCLETIME_MIN] = m_phy_caps.min_pio_cycletime_no_iordy; // Without IORDY
    idf[IDE_IDENTIFY_OFFSET_PIO_CYCLETIME_IORDY] = m_phy_caps.min_pio_cycletime_with_iordy; // With IORDY

    idf[IDE_IDENTIFY_OFFSET_STANDARD_VERSION_MAJOR] = 0x0078; // Version ATAPI-6
    idf[IDE_IDENTIFY_OFFSET_STANDARD_VERSION_MINOR] = 0x0019; // Minor version rev 3a
    idf[IDE_IDENTIFY_OFFSET_COMMAND_SET_SUPPORT_1] = 0x0004; //  Removable device command sets supported
    idf[IDE_IDENTIFY_OFFSET_COMMAND_SET_SUPPORT_2] = 0x4000;
    idf[IDE_IDENTIFY_OFFSET_COMMAND_SET_SUPPORT_3] = 0x4000;
    idf[IDE_IDENTIFY_OFFSET_COMMAND_SET_ENABLED_1] = 0x0004;

    idf[IDE_IDENTIFY_OFFSET_MODEINFO_ULTRADMA]  = (m_phy_caps.max_udma_mode >= 0) ? 0x0001 : 0;
    idf[IDE_IDENTIFY_OFFSET_MODEINFO_ULTRADMA] |= (m_ata_state.udma_mode == 0)  ? (1 << 8) : 0;


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
            logmsg("IDERigidDevice::cmd_identify_packet_device() response write timeout");
            ide_phy_stop_transfers();
            return false;
        }
    }
    regs->error = 0;
    regs->status = IDE_STATUS_DEVRDY | IDE_STATUS_DSC;
    ide_phy_set_regs(regs);
    return true;
}


bool IDERigidDevice::cmd_recalibrate(ide_registers_t *regs)
{

    regs->lba_low = is_lba_mode(regs) ? 0 : 1;
    regs->lba_high = 0;
    regs->lba_mid =  0;
    regs->device &= 0xF0;
    ide_phy_set_regs(regs);
    ide_phy_assert_irq(IDE_STATUS_DEVRDY | IDE_STATUS_DSC); 
    return true;
}

void IDERigidDevice::handle_event(ide_event_t evt)
{
    if (evt == IDE_EVENT_HWRST || evt == IDE_EVENT_SWRST)
    {
        if (evt == IDE_EVENT_HWRST)
        {
            m_ata_state.udma_mode = -1;
        }

        set_device_signature(0, true);
    }
}

// Set the packet device signature values to PHY registers
// See T13/1410D revision 3a section 9.12 Signature and persistence
bool IDERigidDevice::set_device_signature(uint8_t error, bool was_reset)
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
    else
    {
        regs.status = IDE_STATUS_BSY;
    }
    ide_phy_set_regs(&regs);

    ide_phy_assert_irq(IDE_STATUS_DEVRDY | IDE_STATUS_DSC);

    return true;
}

// Set the packet device signature values to PHY registers
// See T13/1410D revision 3a section 9.12 Signature and persistence
void IDERigidDevice::fill_device_signature(ide_registers_t *regs)
{
    regs->lba_low = 0x01;
    regs->lba_mid = 0x00;
    regs->lba_high = 0x00;
    regs->sector_count = 0x01;
    regs->device = 0x00;
}

void IDERigidDevice::lba2chs(const uint32_t lba, uint16_t &cylinder, uint8_t &head, uint8_t &sector)
{
    sector = (lba % m_devinfo.sectors_per_track) + 1;
    cylinder = (lba / m_devinfo.sectors_per_track) / m_devinfo.heads;
    head = (lba / m_devinfo.sectors_per_track) % m_devinfo.heads;
}

bool IDERigidDevice::ata_send_chunked_data(const uint8_t *data, size_t blocksize, size_t num_blocks)
{
    // Report data for command responses
    dbgmsg("---- ATA send ", (int)num_blocks, "x", (int)blocksize, " bytes: ",
        bytearray((const uint8_t*)data, blocksize * num_blocks));

    size_t max_blocksize = m_phy_caps.max_blocksize;

    for (size_t i = 0; i < num_blocks; i++)
    {
        size_t sent = 0;
        while (sent + max_blocksize < blocksize)
        {
            // Send smaller pieces when max block size exceeded
            if (!ata_send_data_block(data + blocksize * i + sent, max_blocksize))
            {
                return false;
            }

            sent += max_blocksize;
        }

        // Send rest as single block (common case)
        if (!ata_send_data_block(data + blocksize * i + sent, blocksize))
        {
            return false;
        }
    }

    return ata_send_wait_finish();
}

ssize_t IDERigidDevice::ata_send_data(const uint8_t *data, size_t blocksize, size_t num_blocks)
{
    if (m_ata_state.data_state == ATA_DATA_WRITE && blocksize == m_ata_state.blocksize)
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
                dbgmsg("ata_send_data(): interrupted");
                return -1;
            }
        }

        return blocks_sent;
    }

    size_t max_blocksize = m_phy_caps.max_blocksize;
    if (blocksize > max_blocksize)
    {
        dbgmsg("-- atapi_send_data_async(): Block size ", (int)blocksize, " exceeds limit ", (int)max_blocksize,
               ", using ata_send_chunked_data() instead");

        if (ata_send_chunked_data(data, blocksize, num_blocks))
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
        if (ata_send_data_block(data, blocksize))
        {
            return 1;
        }
        else
        {
            return -1;
        }
    }
}

bool IDERigidDevice::ata_send_data_block(const uint8_t *data, uint16_t blocksize)
{
    // dbgmsg("---- Send data block ", (uint32_t)data, " ", (int)blocksize, " udma_mode:", m_ata_state.udma_mode);

    if (m_ata_state.data_state != ATA_DATA_WRITE || blocksize != m_ata_state.blocksize) 
    {
        ata_send_wait_finish();
        m_ata_state.blocksize = blocksize;
        m_ata_state.data_state = ATA_DATA_WRITE;

        // Set number bytes to transfer to registers
        ide_registers_t regs = {};
        ide_phy_get_regs(&regs);
        regs.status = IDE_STATUS_BSY;
        ide_phy_set_regs(&regs);

        // Start data transfer
        int udma_mode = (m_ata_state.dma_requested ? m_ata_state.udma_mode : -1);
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
                logmsg("IDERigidDevice::ata_send_data_block() data write timeout");
                return false;
            }
            if (ide_phy_is_command_interrupted())
            {
                dbgmsg("IDERigidDevice::ata_send_data_block() interrupted");
                return false;
            }
        }

        ide_phy_write_block(data, blocksize);
    }

    return true;
}

bool IDERigidDevice::ata_send_wait_finish()
{
    // Wait for transfer to finish
    uint32_t start = millis();
    while (!ide_phy_is_write_finished())
    {
        platform_poll();
        if ((uint32_t)(millis() - start) > 10000)
        {
            logmsg("IDERigidDevice::ata_send_wait_finish() data write timeout");
            return false;
        }

        if (ide_phy_is_command_interrupted())
        {
            dbgmsg("IDERigidDevice::ata_send_wait_finish() interrupted");
            return false;
        }
    }
    return true;
}


bool IDERigidDevice::ata_recv_data(uint8_t *data, size_t blocksize, size_t num_blocks)
{
    size_t max_blocksize = m_phy_caps.max_blocksize;
    if (blocksize > max_blocksize)
    {
        // Have to split the blocks for phy
        size_t split = (blocksize + max_blocksize - 1) / max_blocksize;
        assert(blocksize % split == 0);
        blocksize /= split;
        num_blocks *= split;
    }
    // Causing timeouts 
    // else
    // {
    //     // Combine blocks for better performance
    //     while (blocksize * 2 < max_blocksize && (num_blocks & 1) == 0)
    //     {
    //         blocksize *= 2;
    //         num_blocks >>= 1;
    //     }
    // }

    // Set number bytes to transfer to registers
    ide_registers_t regs = {};
    ide_phy_get_regs(&regs);
    regs.status = IDE_STATUS_BSY;
    // regs.sector_count = 0; // Data transfer to device
    // regs.lba_mid = (uint8_t)blocksize;
    // regs.lba_high = (uint8_t)(blocksize >> 8);
    ide_phy_set_regs(&regs);

    // Start data transfer for first block
    int udma_mode = (m_ata_state.dma_requested ? m_ata_state.udma_mode : -1);
    ide_phy_start_read(blocksize, udma_mode);

    // Receive blocks
    for (size_t i = 0; i < num_blocks; i++)
    {
        uint32_t start = millis();
        while (!ide_phy_can_read_block())
        {
            if ((uint32_t)(millis() - start) > 10000)
            {
                logmsg("IDERigidDevice::ata_recv_data read timeout on block ", (int)(i + 1), "/", (int)num_blocks);
                ide_phy_stop_transfers();
                return false;
            }

            if (ide_phy_is_command_interrupted())
            {
                dbgmsg("IDERigidDevice::ata_recv_data() interrupted");
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

bool IDERigidDevice::ata_recv_data_block(uint8_t *data, uint16_t blocksize)
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
    int udma_mode = (m_ata_state.dma_requested ? m_ata_state.udma_mode : -1); 
    ide_phy_start_read(blocksize, udma_mode);

    uint32_t start = millis();
    while (!ide_phy_can_read_block())
    {
        if ((uint32_t)(millis() - start) > 10000)
        {
            logmsg("IDERigidDevice::atapi_recv_data_block(", (int)blocksize, ") read timeout");
            ide_phy_stop_transfers();
            return false;
        }

        if (ide_phy_is_command_interrupted())
        {
            dbgmsg("IDERigidDevice::atapi_recv_data_block() interrupted");
            return false;
        }
    }

    ide_phy_read_block(data, blocksize);
    ide_phy_stop_transfers();
    return true;
}


// IDEImage implementation calls this when new data is available from file.
// This will send the data to IDE bus.
ssize_t IDERigidDevice::read_callback(const uint8_t *data, size_t blocksize, size_t num_blocks)
{
    platform_poll();
    return ata_send_data(data, blocksize, num_blocks);
}

// Called by IDEImage to request reception of more data from IDE bus
ssize_t IDERigidDevice::write_callback(uint8_t *data, size_t blocksize, size_t num_blocks)
{
    if (ata_recv_data(data, blocksize, num_blocks))
    {
        return num_blocks;
    }
    else
    {
        logmsg("IDERigidDevice::write_callback(", (int)blocksize, ", ", (int)num_blocks, ") failed");
        return -1;
    }
}

