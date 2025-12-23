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
 * Under Section 7 of GPL version 3, you are granted additional
 * permissions described in the ZuluIDE Hardware Support Library Exception
 * (GPL-3.0_HSL_Exception.md), as published by Rabbit Hole Computing™.
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
#include <zuluide/images/image_iterator.h>
#include <status/status_controller.h>

#define ZIP100_SECTORSIZE 512
#define ZIP100_SECTORCOUNT 196608

extern zuluide::status::StatusController g_StatusController;

void IDEZipDrive::initialize(int devidx)
{
    IDEATAPIDevice::initialize(devidx);

    m_devinfo.devtype = ATAPI_DEVTYPE_DIRECT_ACCESS;
    m_devinfo.removable = true;
    m_devinfo.writable = true;
    m_devinfo.bytes_per_sector = ZIP100_SECTORSIZE;

    if (m_image && m_image->get_drive_type() ==  drive_type_t::DRIVE_TYPE_ZIP250)
    {
        set_inquiry_strings("IOMEGA", "ZIP 250", "41.S");
        set_ident_strings("IOMEGA  ZIP 250       ATAPI", "00DB47B188C61421", "41.S");
    }
    else
    {
        set_inquiry_strings("IOMEGA", "ZIP 100", "14.A");
        set_ident_strings("IOMEGA  ZIP 100       ATAPI", "", "14.A");
    }

    m_devinfo.num_profiles = 1;
    m_devinfo.profiles[0] = ATAPI_PROFILE_REMOVABLE;
    m_devinfo.current_profile = ATAPI_PROFILE_REMOVABLE;

    m_removable.reinsert_media_after_eject = ini_getbool("IDE", "reinsert_media_after_eject", true, CONFIGFILE);
    m_removable.reinsert_media_on_inquiry =  ini_getbool("IDE", "reinsert_media_on_inquiry", true, CONFIGFILE);

    m_media_status_notification = false;

    m_zip_disk_info.button_pressed = false;
    memset(m_zip_disk_info.serial_string, ' ', sizeof(m_zip_disk_info.serial_string) -1);
    m_zip_disk_info.serial_string[sizeof(m_zip_disk_info.serial_string)- 1] = '\0';
}

// Capacity is based on image size
uint64_t IDEZipDrive::capacity()
{
    return (m_image ? m_image->capacity() : 0);
}


bool IDEZipDrive::handle_command(ide_registers_t *regs)
{
    switch (regs->command)
    {
        // Supported IDE commands
        case IDE_CMD_SET_FEATURES: return cmd_set_features(regs);
        case IDE_CMD_GET_MEDIA_STATUS: return cmd_get_media_status(regs);
        default: return IDEATAPIDevice::handle_command(regs);
    }
}

void IDEZipDrive::eject_media()
{
    char filename[MAX_FILE_PATH+1];
    if (m_image && m_image->get_image_name(filename, sizeof(filename)))
        logmsg("Device ejecting media: \"", filename, "\"");
    else
        logmsg("Eject requested, no media to eject");
    g_StatusController.SetIsCardPresent(false);
    m_removable.ejected = true;
}


void IDEZipDrive::button_eject_media()
{
    if (m_removable.loaded_without_media)
    {
        m_removable.loaded_without_media = false;
        if(m_removable.load_first_image_cb) 
        {
            m_removable.load_first_image_cb();
            m_removable.load_first_image_cb = nullptr;
        }
        loaded_new_media();
    }
    else if (m_removable.prevent_removable)
        m_zip_disk_info.button_pressed = true;
    else
        eject_media();
}


void IDEZipDrive::insert_media(IDEImage *image)
{
    zuluide::images::ImageIterator img_iterator;
    char filename[MAX_FILE_PATH+1];

    img_iterator.Reset();
    if (!img_iterator.IsEmpty())
    {
        if (image && image->get_image_name(filename, sizeof(filename)))
        {
            if (!img_iterator.MoveToFile(filename))
                img_iterator.MoveNext();
        }
        else
            img_iterator.MoveNext();

        g_ide_imagefile.clear();
        if (g_ide_imagefile.open_file(img_iterator.Get().GetFilename().c_str()))
        {
            logmsg("-- Device loading media: \"", img_iterator.Get().GetFilename().c_str(), "\"");
            m_removable.ejected = false;
            set_image(&g_ide_imagefile);
            loaded_new_media();
        }
    }
        img_iterator.Cleanup();
}

bool IDEZipDrive::set_load_deferred(const char* image_name)
{
    if (!m_removable.ignore_prevent_removal && m_removable.prevent_removable)
    {
        dbgmsg("Loading file deferred, host is preventing media from being ejected: \"", image_name, "\"");
        strncpy(m_removable.deferred_image_name, image_name, sizeof(m_removable.deferred_image_name) - 1);
        m_removable.is_load_deferred = true;
        g_StatusController.SetIsDeferred(true);
        m_zip_disk_info.button_pressed = true;
        return true;
    }
    return false;
}


bool IDEZipDrive::is_load_deferred()
{
    return m_removable.is_load_deferred;
}

bool IDEZipDrive::cmd_set_features(ide_registers_t *regs)
{
    uint8_t feature = regs->feature;
    regs->error = 0;
    bool has_feature = false;
    if (feature == IDE_SET_FEATURE_DISABLE_STATUS_NOTIFICATION)
    {
        dbgmsg("-- Disable status notification");
        m_media_status_notification = false;
        has_feature = true;
    }

    if (has_feature)
    {
        if (regs->error == 0)
        {
            ide_phy_assert_irq(IDE_STATUS_DEVRDY | IDE_STATUS_DSC);
        }
        else
        {
            ide_phy_assert_irq(IDE_STATUS_DEVRDY | IDE_STATUS_DSC | IDE_STATUS_ERR);
        }
        return true;
    }

    return IDEATAPIDevice::cmd_set_features(regs);
}
void IDEZipDrive::set_image(IDEImage *image)
{
    if (image)
    {
        char filename[MAX_FILE_PATH + 1] = {0};
        image->get_image_name(filename, sizeof(filename));
        uint64_t actual_size = image->capacity();
        uint64_t expected_size = ZIP100_SECTORSIZE * ZIP100_SECTORCOUNT;
        // use filename for Zip disk serial string
        size_t filename_len = strlen(filename);
        size_t serial_len = sizeof(m_zip_disk_info.serial_string) - 1;
        if ( filename_len > serial_len)
            filename_len = serial_len;
        memset(m_zip_disk_info.serial_string, ' ', serial_len);
        m_zip_disk_info.serial_string[serial_len] = '\0';
        memcpy(m_zip_disk_info.serial_string, filename, filename_len);

        m_zip_disk_info.button_pressed = false;

        if (actual_size < expected_size)
        {
            logmsg("-- WARNING: Image file ", filename, " is only ", (int)actual_size, " bytes, expecting more than or equal to", (int)expected_size, " bytes");
        }

    }

    IDEATAPIDevice::set_image(image);
}

// Responds with 512 bytes of identification data
bool IDEZipDrive::cmd_identify_packet_device(ide_registers_t *regs)
{
    uint16_t idf[256] = {0};

    IDEATAPIDevice::atapi_identify_packet_device_response(idf);

    idf[IDE_IDENTIFY_OFFSET_GENERAL_CONFIGURATION] = 0x80A0;

    if (m_image == nullptr ||  m_image->get_drive_type() == drive_type_t::DRIVE_TYPE_ZIP100)
    {
        idf[IDE_IDENTIFY_OFFSET_CAPABILITIES_1] = 0x0E00;
        idf[IDE_IDENTIFY_OFFSET_PIO_MODE_ATA1] = 0;
        idf[IDE_IDENTIFY_OFFSET_MODE_INFO_VALID] = 0x0002;
        idf[IDE_IDENTIFY_OFFSET_MODEINFO_MULTIWORD] = 0x0000;
        idf[IDE_IDENTIFY_OFFSET_MODEINFO_PIO] = 0;
        idf[IDE_IDENTIFY_OFFSET_MULTIWORD_CYCLETIME_MIN] = 0;
        idf[IDE_IDENTIFY_OFFSET_MULTIWORD_CYCLETIME_REC] = 0;
        idf[IDE_IDENTIFY_OFFSET_PIO_CYCLETIME_MIN] =    0x01f4;
        idf[IDE_IDENTIFY_OFFSET_PIO_CYCLETIME_IORDY] =  0x01f4;
        idf[IDE_IDENTIFY_OFFSET_STANDARD_VERSION_MAJOR] = 0;
        idf[IDE_IDENTIFY_OFFSET_STANDARD_VERSION_MINOR] = 0;
        idf[IDE_IDENTIFY_OFFSET_MODEINFO_ULTRADMA] = 0;
        idf[IDE_IDENTIFY_OFFSET_REMOVABLE_MEDIA_SUPPORT] = 0x0101; // PACKET, Removable device command sets supported
    }
    else if (m_image && m_image->get_drive_type() == drive_type_t::DRIVE_TYPE_ZIP250)
    {
        idf[IDE_IDENTIFY_OFFSET_CAPABILITIES_1] = 0x0F00;
        idf[IDE_IDENTIFY_OFFSET_PIO_MODE_ATA1] = 0x0200;
        idf[IDE_IDENTIFY_OFFSET_MODE_INFO_VALID] = 0x0006;
        idf[IDE_IDENTIFY_OFFSET_MODEINFO_MULTIWORD] = 0x0203;
        idf[IDE_IDENTIFY_OFFSET_MODEINFO_PIO] = 0x0001;
        idf[IDE_IDENTIFY_OFFSET_MULTIWORD_CYCLETIME_MIN] = 0x0096;
        idf[IDE_IDENTIFY_OFFSET_MULTIWORD_CYCLETIME_REC] = 0x0096;
        idf[IDE_IDENTIFY_OFFSET_PIO_CYCLETIME_MIN] =  0x00B4;
        idf[IDE_IDENTIFY_OFFSET_PIO_CYCLETIME_IORDY] =  0x00B4;
        idf[IDE_IDENTIFY_OFFSET_STANDARD_VERSION_MAJOR] = 0x0030; // Version ATAPI-5 and 4
        idf[IDE_IDENTIFY_OFFSET_STANDARD_VERSION_MINOR] = 0x0015; // Minor version
        idf[IDE_IDENTIFY_OFFSET_REMOVABLE_MEDIA_SUPPORT] = 0x0001; // PACKET, Removable device command sets supported
    }
    else
    {
        // Zip 750 settings
        logmsg("Unsupported Zip Drive type");
        return false;
    }

    idf[IDE_IDENTIFY_OFFSET_CAPABILITIES_2] = 0x4002;
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

    // Calculate checksum
    // See 8.15.61 Word 255: Integrity word
    uint8_t checksum = 0xA5;
    for (int i = 0; i < 255; i++)
    {
        checksum += (idf[i] & 0xFF) + (idf[i] >> 8);
    }
    checksum = -checksum;
    idf[IDE_IDENTIFY_OFFSET_INTEGRITY_WORD] = ((uint16_t)checksum << 8) | 0xA5;

    regs->error = 0;
    ide_phy_set_regs(regs);
    ide_phy_start_write(sizeof(idf));
    ide_phy_write_block((uint8_t*)idf, sizeof(idf));

    uint32_t start = millis();
    while (!ide_phy_is_write_finished())
    {
        if ((uint32_t)(millis() - start) > 10000)
        {
            logmsg("IDEZipDriveDevice::cmd_identify_packet_device() response write timeout");
            ide_phy_stop_transfers();
            return false;
        }
    }

    ide_phy_assert_irq(IDE_STATUS_DEVRDY | IDE_STATUS_DSC);

    return true;
}

bool IDEZipDrive::cmd_get_media_status(ide_registers_t *regs)
{
    if (!m_media_status_notification)
    {
        // media status notification disabled
        ide_phy_assert_irq(IDE_STATUS_DEVRDY | IDE_STATUS_DSC);
        return true;
    }
    else
    {
        // Error register bits
        // 6: Write protect
        // 5: Media change
        // 3: Media change request
        // 2: Abort
        // 1: No Media
        regs->error = 0;
    }
    if (regs->error == 0)
    {
        ide_phy_assert_irq(IDE_STATUS_DEVRDY | IDE_STATUS_DSC);
    }
    else
    {
        ide_phy_assert_irq(IDE_STATUS_DEVRDY | IDE_STATUS_DSC | IDE_STATUS_ERR);
    }
    return true;
}


bool IDEZipDrive::handle_atapi_command(const uint8_t *cmd)
{
    switch (cmd[0])
    {
        case ATAPI_CMD_FORMAT_UNIT: return atapi_format_unit(cmd);
        case ATAPI_CMD_READ_FORMAT_CAPACITIES: return atapi_read_format_capacities(cmd);
        case ATAPI_CMD_VERIFY10: return atapi_verify(cmd);
        case ATAPI_CMD_VENDOR_0x06: return atapi_zip_disk_0x06(cmd);
        case ATAPI_CMD_VENDOR_0x0D: return atapi_zip_disk_0x0D(cmd);
        default:
            return IDEATAPIDevice::handle_atapi_command(cmd);
    }
}

bool IDEZipDrive::atapi_format_unit(const uint8_t *cmd)
{
    if (!is_medium_present()) return atapi_cmd_not_ready_error();
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
    write_be32(&buf[4], capacity_lba());
    buf[8] = 0x02;
    write_be24(&buf[9], 0x000200);

    atapi_send_data(buf, std::min<uint32_t>(allocationLength, len));
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

    uint8_t *inquiry = m_buffer.bytes;
    uint8_t count = 0;


    if (m_image == nullptr ||  m_image->get_drive_type() == drive_type_t::DRIVE_TYPE_ZIP100)
    {
        // Taken from a IDE Zip Drive 100
        count = 122;
        memset(inquiry, 0x00, count);
        inquiry[0] = 0x00;
        inquiry[1] = 0x80;
        inquiry[2] = 0x00;
        inquiry[3] = 0x01;
        inquiry[4] = 0x75;
        inquiry[5] = 0x00;
        inquiry[6] = 0x00;
        inquiry[7] = 0x00;
        memcpy(&inquiry[ATAPI_INQUIRY_VENDOR], m_devinfo.atapi_vendor, 8);
        memcpy(&inquiry[ATAPI_INQUIRY_PRODUCT], m_devinfo.atapi_product, 16);
        memcpy(&inquiry[ATAPI_INQUIRY_REVISION], m_devinfo.atapi_version, 4);
        // vendor specific data
        inquiry[36] = 0x30;
        inquiry[37] = 0x39;
        inquiry[38] = 0x2F;
        inquiry[39] = 0x30;
        inquiry[40] = 0x34;
        inquiry[41] = 0x2F;
        inquiry[42] = 0x39;
        inquiry[43] = 0x38;

        // vendor specific copyright
        inquiry[96] = 0x28;
        inquiry[97] = 0x63;
        inquiry[98] = 0x29;
        inquiry[99] = 0x20;
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
        inquiry[117] = 0x31;
        inquiry[118] = 0x39;
        inquiry[119] = 0x39;
        inquiry[120] = 0x37;
        inquiry[121] = 0x20;

    }
    else if (m_image && m_image->get_drive_type() == drive_type_t::DRIVE_TYPE_ZIP250)
    {
        count = 122;
        memset(inquiry, 0x00, count);
    // Copied direcetly from an IDE Zip drive 250
        inquiry[1]   = 0x80;
        inquiry[3]   = 0x01;
        inquiry[4]   = 0x75;
        memcpy(&inquiry[ATAPI_INQUIRY_VENDOR], m_devinfo.atapi_vendor, 8);
        memcpy(&inquiry[ATAPI_INQUIRY_PRODUCT], m_devinfo.atapi_product, 16);
        memcpy(&inquiry[ATAPI_INQUIRY_REVISION], m_devinfo.atapi_version, 4);
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
    }
    if (req_bytes < count) count = req_bytes;

    atapi_send_data(inquiry, count);

    if (m_removable.reinsert_media_on_inquiry)
    {
        insert_next_media(m_image);
    }

    return atapi_cmd_ok();
}

bool IDEZipDrive::atapi_start_stop_unit(const uint8_t *cmd)
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
            {
                if (m_removable.is_load_deferred && m_image)
                {
                    if (m_removable.deferred_image_name[0] != '\0')
                    {
                        g_ide_imagefile.clear();
                        g_ide_imagefile.open_file(m_removable.deferred_image_name);
                        m_removable.is_load_deferred = false;
                        g_StatusController.SetIsDeferred(false);
                        insert_media(&g_ide_imagefile);
                    }
                    else
                    {
                        g_StatusController.SetIsDeferred(false);
                        m_removable.is_load_deferred = false;
                        m_zip_disk_info.button_pressed = false;
                        eject_media();
                    }
                }
                else
                {
                    eject_media();
                }
            }
        }
        // Load condition
        else
        {
            insert_next_media(m_image);
        }
    }
    return atapi_cmd_ok();

}

bool IDEZipDrive::atapi_zip_disk_0x06(const uint8_t *cmd)
{

    if (cmd[2] != 0x02)
    {
        dbgmsg("Vendor 0x06 sub command ", cmd[0], " unsupported");
    }
    // Currently zip disks of size 100MB info is returned 
    const uint8_t count = 64;
    uint8_t *buf = m_buffer.bytes;
    memset(buf, 0x00, count);

    // These seem to differ between different drives, even of the same type
    if (m_image &&  m_image->get_drive_type() == drive_type_t::DRIVE_TYPE_ZIP100)
    {
        buf[0x3E] = 0x00;
        buf[0x3F] = 0x12;
    }
    else if(m_image && m_image->get_drive_type() == drive_type_t::DRIVE_TYPE_ZIP250)
    {
        buf[62] = 0x10;
        buf[63] = 0x10;
    }

    if (!is_medium_present())
    {
        // no disk
        buf[0] = 0x02;
        buf[1] = 0x3E;
        buf[2] = 0x04;
        buf[11] = 0x02;

    }
    else
    {
        if (m_image && m_image->get_drive_type() == drive_type_t::DRIVE_TYPE_ZIP100)
        {
            // 100MB disk
            buf[0x00] = 0x02;
            buf[0x01] = 0x3E;
            buf[0x02] = m_zip_disk_info.button_pressed ? 0x01 : 0x00;
            buf[0x03] = 0x02;
            buf[0x04] = 0x00;
            buf[0x05] = 0x00;
            buf[0x06] = 0x02;
            buf[0x07] = 0xFF;
            buf[0x08] = 0xFF;
            buf[0x09] = 0x00;
            buf[0x0A] = 0x00;
            buf[0x0B] = 0x02;
            buf[0x0C] = 0x00;
            buf[0x0D] = 0x00;
            buf[0x0E] = 0x7E;
            buf[0x0F] = 0x00;
            buf[0x10] = 0x00;
            buf[0x11] = 0x00;
            buf[0x12] = 0x7E;
            buf[0x13] = 0x00;
            buf[0x14] = 0x00;
            buf[0x15] = 0x00;
            // serial number
            memcpy(buf + 0x16, m_zip_disk_info.serial_string, sizeof(m_zip_disk_info.serial_string) -1);
            buf[0x2F] = 0x41;
            buf[0x30] = 0x34;
            buf[0x31] = 0x32;
            buf[0x32] = 0x5A;
            buf[0x33] = 0x50;
            buf[0x34] = 0x31;
            buf[0x35] = 0x38;
            buf[0x36] = 0x45;
            buf[0x37] = 0x31;
            buf[0x38] = 0x31;
            buf[0x39] = 0x32;
            buf[0x3A] = 0x20;
            buf[0x3B] = 0x20;
            buf[0x3C] = 0x20;
            buf[0x3D] = 0x20;
        }
        else if (m_image && m_image->get_drive_type() == drive_type_t::DRIVE_TYPE_ZIP250)
        {
            // 250 disk
            buf[0x00] = 0x02;
            buf[0x01] = 0x3E;
            buf[0x02] = m_zip_disk_info.button_pressed ? 0x01 : 0x00;
            buf[0x03] = 0x02;
            buf[0x06] = 0x02;
            buf[0x07] = 0xFF;
            buf[0x08] = 0xFF;
            buf[0x09] = 0x00;
            buf[0x0A] = 0x00;
            buf[0x0B] = 0x02;
            buf[0x0C] = 0x00;
            buf[0x0D] = 0x00;
            buf[0x0E] = 0x7D;
            buf[0x0F] = 0x00;
            buf[0x10] = 0x01;
            buf[0x11] = 0x00;
            buf[0x12] = 0x78;
            buf[0x13] = 0x00;
            buf[0x14] = 0x06;
            buf[0x15] = 0x00;
            buf[0x16] = 0x35;
            buf[0x17] = 0x32;
            buf[0x18] = 0x33;
            buf[0x19] = 0x34;
            buf[0x1A] = 0x30;
            buf[0x1B] = 0x32;
            buf[0x1C] = 0x30;
            buf[0x1D] = 0x32;
            buf[0x1E] = 0x31;
            buf[0x1F] = 0x34;
            buf[0x20] = 0x37;
            buf[0x21] = 0x39;
            buf[0x22] = 0x37;
            buf[0x23] = 0x33;
            buf[0x24] = 0x31;
            buf[0x25] = 0x34;
            buf[0x26] = 0x30;
            buf[0x27] = 0x32;
            buf[0x28] = 0x5A;
            buf[0x29] = 0x49;
            buf[0x2A] = 0x50;
            buf[0x2B] = 0x31;
            buf[0x2C] = 0x20;
            buf[0x2D] = 0x20;
            buf[0x2E] = 0x20;
            buf[0x2F] = 0x4B;
            buf[0x30] = 0x41;
            buf[0x31] = 0x4D;
            buf[0x32] = 0x39;
            buf[0x33] = 0x35;
            buf[0x34] = 0x30;
            buf[0x35] = 0x30;
            buf[0x36] = 0x45;
            buf[0x37] = 0x33;
            buf[0x38] = 0x31;
            buf[0x39] = 0x31;
            buf[0x3A] = 0x20;
            buf[0x3B] = 0x20;
            buf[0x3C] = 0x20;
            buf[0x3D] = 0x20;
        }
    }
    atapi_send_data(buf, count);
    return atapi_cmd_ok();
}

bool IDEZipDrive::atapi_zip_disk_0x0D(const uint8_t *cmd)
{
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
        buffer[4] = 0x3c;
        buffer[5] = 0x0f;
        return 6;
    }

    return IDEATAPIDevice::atapi_get_mode_page(page_ctrl, page_idx, buffer, max_bytes);
}