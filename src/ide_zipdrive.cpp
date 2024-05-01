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

#include "ide_zipdrive.h"
#include "ide_utils.h"
#include "atapi_constants.h"
#include "ZuluIDE_log.h"
#include "ZuluIDE_config.h"
#include "ZuluIDE.h"
#include <string.h>
#include <strings.h>
#include <minIni.h>

#define ZIP100_SECTORSIZE 512
#define ZIP100_SECTORCOUNT 196608

void IDEZipDrive::initialize(int devidx)
{
    IDEATAPIDevice::initialize(devidx);

    m_devinfo.devtype = ATAPI_DEVTYPE_DIRECT_ACCESS;
    m_devinfo.removable = true;
    m_devinfo.writable = true;
    m_devinfo.bytes_per_sector = ZIP100_SECTORSIZE;
    
    strncpy(m_devinfo.ide_vendor, "IOMEGA", sizeof(m_devinfo.ide_vendor));
    strncpy(m_devinfo.ide_product, "ZIP 100", sizeof(m_devinfo.ide_product));
    memcpy(m_devinfo.ide_revision, "14.A", sizeof(m_devinfo.ide_revision));
    strncpy(m_devinfo.atapi_model, "IOMEGA ZIP 100", sizeof(m_devinfo.atapi_model));
    memcpy(m_devinfo.atapi_revision, "14.A", sizeof(m_devinfo.atapi_revision));

    m_devinfo.num_profiles = 1;
    m_devinfo.profiles[0] = ATAPI_PROFILE_REMOVABLE;
    m_devinfo.current_profile = ATAPI_PROFILE_REMOVABLE;

    m_removable.reinsert_media_after_eject = ini_getbool("IDE", "reinsert_media_after_eject", true, CONFIGFILE);
    m_removable.reinsert_media_on_inquiry =  ini_getbool("IDE", "reinsert_media_on_inquiry", true, CONFIGFILE);
}

// We always have the same capacity, no matter image size
uint64_t IDEZipDrive::capacity()
{
    return (m_image ? ZIP100_SECTORCOUNT * ZIP100_SECTORSIZE : 0);
}

void IDEZipDrive::set_image(IDEImage *image)
{
    if (image)
    {
        char filename[MAX_FILE_PATH] = "";
        image->get_filename(filename, sizeof(filename));
        uint64_t actual_size = image->capacity();
        uint64_t expected_size = ZIP100_SECTORSIZE * ZIP100_SECTORCOUNT;
        if (actual_size < expected_size)
        {
            logmsg("-- WARNING: Image file ", filename, " is only ", (int)actual_size, " bytes, expecting ", (int)expected_size, " bytes");
        }
        else if (actual_size > expected_size)
        {
            logmsg("-- Image file ", filename, " is ", (int)actual_size, " bytes, ignoring anything past ", (int)expected_size);
        }
    }

    IDEATAPIDevice::set_image(image);

    // Notify host of media change
    m_atapi_state.unit_attention = true;

    if (!image)
    {
        m_devinfo.media_status_events = ATAPI_MEDIA_EVENT_EJECTREQ;
        m_devinfo.medium_type = ATAPI_MEDIUM_NONE;
    }
    else
    {
        m_devinfo.media_status_events = ATAPI_MEDIA_EVENT_NEW;
        m_devinfo.medium_type = ATAPI_MEDIUM_UNKNOWN;
    }
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
bool IDEZipDrive::cmd_identify_packet_device(ide_registers_t *regs)
{
    uint16_t idf[256] = {0};

    idf[IDE_IDENTIFY_OFFSET_GENERAL_CONFIGURATION] = 0x80A0;
    copy_id_string(&idf[IDE_IDENTIFY_OFFSET_SERIAL_NUMBER], 10, "80DB40BF14061510");
    copy_id_string(&idf[IDE_IDENTIFY_OFFSET_FIRMWARE_REV], 4, "41.S");
    copy_id_string(&idf[IDE_IDENTIFY_OFFSET_MODEL_NUMBER], 20, "IOMEGA  ZIP 250       ATAPI");
    idf[IDE_IDENTIFY_OFFSET_CAPABILITIES_1] = 0x0F00;
    idf[IDE_IDENTIFY_OFFSET_CAPABILITIES_2] = 0x4002;
    idf[IDE_IDENTIFY_OFFSET_PIO_MODE_ATA1] = 0x0200;
    idf[IDE_IDENTIFY_OFFSET_MODE_INFO_VALID] = 0x0006;
    idf[IDE_IDENTIFY_OFFSET_MODEINFO_MULTIWORD] = 0x0203; //  0; // force unsupported 

    idf[IDE_IDENTIFY_OFFSET_MODEINFO_PIO] = 0x0001;
    idf[IDE_IDENTIFY_OFFSET_MULTIWORD_CYCLETIME_MIN] = 0x0096;
    idf[IDE_IDENTIFY_OFFSET_MULTIWORD_CYCLETIME_REC] = 0x0096;
    idf[IDE_IDENTIFY_OFFSET_PIO_CYCLETIME_MIN] =  0x00B4;
    idf[IDE_IDENTIFY_OFFSET_PIO_CYCLETIME_IORDY] =  0x00B4;
    idf[IDE_IDENTIFY_OFFSET_STANDARD_VERSION_MAJOR] = 0x0030; // Version ATAPI-5 and 4
    idf[IDE_IDENTIFY_OFFSET_STANDARD_VERSION_MINOR] = 0x0015; // Minor version
    idf[IDE_IDENTIFY_OFFSET_MODEINFO_ULTRADMA] = 0x0007; // Zip250 // 0x0001; // UDMA 0 mode max // 
    idf[IDE_IDENTIFY_OFFSET_REMOVABLE_MEDIA_SUPPORT] = 0x0001; // PACKET, Removable device command sets supported
    // vendor specific - Copyright notice
    idf[129] = 0x2863;
    idf[130] = 0x2920;
    idf[131] = 0x436F;
    idf[132] = 0x7079;
    idf[133] = 0x7269;
    idf[134] = 0x6768;
    idf[135] = 0x7420;
    idf[136] = 0x494F;
    idf[137] = 0x4D45;
    idf[138] = 0x4741;
    idf[139] = 0x2032;
    idf[140] = 0x3030;
    idf[141] = 0x3020;
    idf[142] = 0x0000;
    idf[143] = 0x3830;
    idf[144] = 0x312F;
    idf[145] = 0x2F34;
    idf[146] = 0x3030;
    idf[255] = 0x1EE7; // \todo delete me, this is just to mark the last 16 bit word

    regs->error = 0;
    ide_phy_set_regs(regs);
    ide_phy_start_write(sizeof(idf));
    ide_phy_write_block((uint8_t*)idf, sizeof(idf));
    platform_set_int_pin(true); 
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
    
    ide_phy_assert_irq(IDE_STATUS_DEVRDY);

    return true;
}

bool IDEZipDrive::handle_atapi_command(const uint8_t *cmd)
{
    switch (cmd[0])
    {
        case ATAPI_CMD_FORMAT_UNIT: return atapi_format_unit(cmd);
        case ATAPI_CMD_READ_FORMAT_CAPACITIES: return atapi_read_format_capacities(cmd);
        case ATAPI_CMD_VERIFY10: return atapi_verify(cmd);

        default:
            return IDEATAPIDevice::handle_atapi_command(cmd);
    }
}

bool IDEZipDrive::atapi_format_unit(const uint8_t *cmd)
{
    // Read format descriptor
    atapi_recv_data_block(m_buffer.bytes, 12);

    dbgmsg("---- Format unit: ", bytearray(m_buffer.bytes, 12));

    return atapi_cmd_ok();
}

bool IDEZipDrive::atapi_read_format_capacities(const uint8_t *cmd)
{
    uint32_t allocationLength = parse_be16(&cmd[7]);

    uint8_t *buf = m_buffer.bytes;
    uint32_t len = 12;
    memset(buf, 0, len);
    buf[3] = 0x08; // Capacity list length (current + one formattable descriptor)
    write_be32(&buf[4], 0x00030000);
    buf[8] = 0x02;
    write_be24(&buf[9], 0x000200);

    atapi_send_data(buf, std::min<uint32_t>(allocationLength, len));
    return atapi_cmd_ok();
}

bool IDEZipDrive::atapi_read_capacity(const uint8_t *cmd)
{
    uint32_t last_lba = 0x0002FFFF;
    uint8_t *buf = m_buffer.bytes;
    write_be32(&buf[0], last_lba);
    write_be32(&buf[4], 512);
    atapi_send_data(buf, 8);
    platform_set_int_pin(true);
    return atapi_cmd_ok();
}

bool IDEZipDrive::atapi_verify(const uint8_t *cmd)
{
    dbgmsg("---- ATAPI VERIFY dummy implementation");
    return atapi_cmd_ok();
}

bool IDEZipDrive::atapi_inquiry(const uint8_t *cmd)
{
    uint8_t req_bytes = cmd[4];
     
    uint8_t inquiry[122] = {0};
    uint8_t count = sizeof(inquiry);

// Copied direcetly from an Apple branded IDE Zip 250 drive
    inquiry[1]   = 0x80;
    inquiry[3]   = 0x01;
    inquiry[4]   = 0x75;
    inquiry[8]   = 0x49;
    inquiry[9]   = 0x4F;
    inquiry[10]  = 0x4D;
    inquiry[11]  = 0x45;
    inquiry[12]  = 0x47;
    inquiry[13]  = 0x41;
    inquiry[14]  = 0x20;
    inquiry[15]  = 0x20;
    inquiry[16]  = 0x5A;
    inquiry[17]  = 0x49;
    inquiry[18]  = 0x50;
    inquiry[19]  = 0x20;
    inquiry[20]  = 0x32;
    inquiry[21]  = 0x35;
    inquiry[22]  = 0x30;
    inquiry[23]  = 0x20;
    inquiry[24]  = 0x20;
    inquiry[25]  = 0x20;
    inquiry[26]  = 0x20;
    inquiry[27]  = 0x20;
    inquiry[28]  = 0x20;
    inquiry[29]  = 0x20;
    inquiry[30]  = 0x20;
    inquiry[31]  = 0x20;
    inquiry[32]  = 0x34;
    inquiry[33]  = 0x31;
    inquiry[34]  = 0x2E;
    inquiry[35]  = 0x53;
    inquiry[36]  = 0x30;
    inquiry[37]  = 0x38;
    inquiry[38]  = 0x2F;
    inquiry[39]  = 0x31;
    inquiry[40]  = 0x34;
    inquiry[41]  = 0x2F;
    inquiry[42]  = 0x30;
    inquiry[43]  = 0x30;
    inquiry[96]  = 0x28;
    inquiry[97]  = 0x63;
    inquiry[98]  = 0x29;
    inquiry[99]  = 0x20;
    inquiry[100] = 0x43;
    inquiry[101] = 0x6F;
    inquiry[102] = 0x70;
    inquiry[103] = 0x79;
    inquiry[104] = 0x72;
    inquiry[105] = 0x69;
    inquiry[106] = 0x67;
    inquiry[107] = 0x68;
    inquiry[108] = 0x74;
    inquiry[109] = 0x20;
    inquiry[110] = 0x49;
    inquiry[111] = 0x4F;
    inquiry[112] = 0x4D;
    inquiry[113] = 0x45;
    inquiry[114] = 0x47;
    inquiry[115] = 0x41;
    inquiry[116] = 0x20;
    inquiry[117] = 0x32;
    inquiry[118] = 0x30;
    inquiry[119] = 0x30;
    inquiry[120] = 0x30;
    inquiry[121] = 0x20;


    if (req_bytes < count) count = req_bytes;
        atapi_send_data(inquiry, count);


    if (m_removable.reinsert_media_on_inquiry)
    {
        insert_media();
    }

    return atapi_cmd_ok();
}

size_t IDEZipDrive::atapi_get_mode_page(uint8_t page_ctrl, uint8_t page_idx, uint8_t *buffer, size_t max_bytes)
{
    if (page_idx == ATAPI_MODESENSE_ERRORRECOVERY)
    {
        buffer[0] = ATAPI_MODESENSE_ERRORRECOVERY;
        buffer[1] = 0x06; // Page length
        buffer[2] = 0xc8;
        buffer[3] = 0x16;
        buffer[4] = 0x00;
        buffer[5] = 0x00;
        buffer[6] = 0x00;
        buffer[7] = 0x00;
        
        if (page_ctrl == 1)
        {
            // Mask out unchangeable parameters
            memset(buffer + 2, 0, 6);
        }

        return 8;
    }
    
    if (page_idx == ATAPI_MODESENSE_FLEXDISK)
    {
        memset(buffer, 0, 32);
        buffer[0] = ATAPI_MODESENSE_FLEXDISK;
        buffer[1] = 0x1E; // Page length
        buffer[2] = 0x80; // Transfer rate
        buffer[3] = 0x00;
        buffer[4] = 0x40; // Heads
        buffer[5] = 0x20; // Sectors
        buffer[6] = 0x02; // Bytes per sector (512)
        buffer[7] = 0x00;
        buffer[8] = 0x00;
        buffer[9] = 0x60; // Cylinders
        buffer[28] = 0x0B; // Rotation rate
        buffer[29] = 0x7D;

        if (page_ctrl == 1)
        {
            // Mask out unchangeable parameters
            memset(buffer + 2, 0, 30);
        }

        return 32;
    }

    if (page_idx == ATAPI_MODESENSE_CACHING)
    {
        buffer[0] = ATAPI_MODESENSE_CACHING;
        buffer[1] = 0x0A; // Page length
        buffer[2] = 0x00; // Write cache off
        buffer[3] = 0x00;
        buffer[4] = 0xFF;
        buffer[5] = 0xFF;
        buffer[6] = 0x00;
        buffer[7] = 0x00;
        buffer[8] = 0xFF;
        buffer[9] = 0xFF;
        buffer[10] = 0xFF;
        buffer[11] = 0xFF;
        
        if (page_ctrl == 1)
        {
            // Mask out unchangeable parameters
            memset(buffer + 2, 0, 10);
        }

        return 12;
    }

    if (page_idx == 0x2F)
    {
        // Unknown vendor page specific to Zip drives
        buffer[0] = 0x2F;
        buffer[1] = 0x04;
        buffer[2] = 0x5c;
        buffer[3] = 0x0f;
        buffer[4] = 0xff;
        buffer[5] = 0x0f;
        return 6;
    }

    return IDEATAPIDevice::atapi_get_mode_page(page_ctrl, page_idx, buffer, max_bytes);
}