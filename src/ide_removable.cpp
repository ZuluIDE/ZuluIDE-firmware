/**
 * ZuluIDE™ - Copyright (c) 2024 Rabbit Hole Computing™
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

#include "ide_removable.h"
#include "ide_utils.h"
#include "atapi_constants.h"
#include "ZuluIDE_log.h"
#include "ZuluIDE_config.h"
#include "ZuluIDE.h"
#include <string.h>
#include <strings.h>
#include <minIni.h>

#define REMOVABLE_SECTORSIZE 512
void IDERemovable::initialize(int devidx)
{
    IDEATAPIDevice::initialize(devidx);

    m_devinfo.devtype = ATAPI_DEVTYPE_DIRECT_ACCESS;
    m_devinfo.removable = true;
    m_devinfo.writable = true;
    m_devinfo.bytes_per_sector = REMOVABLE_SECTORSIZE;

    set_inquiry_strings("ZULUIDE", "REMOVABLE", "1.0");
    set_ident_strings("ZULUIDE REMOVABLE", "1234567890", "1.0");

    m_devinfo.num_profiles = 1;
    m_devinfo.profiles[0] = ATAPI_PROFILE_REMOVABLE;
    m_devinfo.current_profile = ATAPI_PROFILE_REMOVABLE;

    m_removable.reinsert_media_after_eject = ini_getbool("IDE", "reinsert_media_after_eject", true, CONFIGFILE);
    m_removable.reinsert_media_on_inquiry =  ini_getbool("IDE", "reinsert_media_on_inquiry", true, CONFIGFILE);
}

uint64_t IDERemovable::capacity()
{
    return (m_image ? (m_image->capacity() / REMOVABLE_SECTORSIZE) * REMOVABLE_SECTORSIZE: 0);
}

void IDERemovable::set_image(IDEImage *image)
{
    if (image)
    {
        char filename[MAX_FILE_PATH] = "";
        image->get_filename(filename, sizeof(filename));

        if (image->capacity() % REMOVABLE_SECTORSIZE != 0)
        {
            logmsg("-- WARNING: Image file ", filename, " is not evenly divisible by sector size ", (int)REMOVABLE_SECTORSIZE, " bytes. Ignoring last partial sector");
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

bool IDERemovable::handle_atapi_command(const uint8_t *cmd)
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

bool IDERemovable::atapi_format_unit(const uint8_t *cmd)
{
    // Read format descriptor
    atapi_recv_data_block(m_buffer.bytes, 12);

    dbgmsg("---- Format unit: ", bytearray(m_buffer.bytes, 12));

    return atapi_cmd_ok();
}

bool IDERemovable::atapi_read_format_capacities(const uint8_t *cmd)
{
    uint32_t allocationLength = parse_be16(&cmd[7]);

    uint8_t *buf = m_buffer.bytes;
    uint32_t len = 4 + 8 + 8;
    memset(buf, 0, len);

    buf[3] = 16; // Capacity list length (current + one formattable descriptor)
    write_be32(&buf[4 + 0], capacity_lba());
    write_be24(&buf[4 + 5], m_devinfo.bytes_per_sector);
    write_be32(&buf[4 + 8 + 4], m_image->capacity() / REMOVABLE_SECTORSIZE);
    write_be24(&buf[4 + 8 + 5], REMOVABLE_SECTORSIZE);

    atapi_send_data(buf, std::min<uint32_t>(allocationLength, len));
    return atapi_cmd_ok();
}

bool IDERemovable::atapi_verify(const uint8_t *cmd)
{
    dbgmsg("---- ATAPI VERIFY dummy implementation");
    return atapi_cmd_ok();
}

size_t IDERemovable::atapi_get_mode_page(uint8_t page_ctrl, uint8_t page_idx, uint8_t *buffer, size_t max_bytes)
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

    return IDEATAPIDevice::atapi_get_mode_page(page_ctrl, page_idx, buffer, max_bytes);
}