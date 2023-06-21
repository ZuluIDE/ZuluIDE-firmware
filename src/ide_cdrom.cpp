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

/*
 * CD-ROM command implementations in this file derive from:
 *
 * ZuluSCSI™ - Copyright (c) 2023 Rabbit Hole Computing™
 * Major contributions by saybur <saybur@users.noreply.github.com>
 *
 * Some of the TOC structures are based on code from:
 * 
 * SCSI2SD V6 - Copyright (C) 2014 Michael McMaster <michael@codesrc.com>
 */

#include "ide_cdrom.h"
#include "ide_utils.h"
#include "atapi_constants.h"
#include "ZuluIDE_log.h"
#include "ZuluIDE_config.h"
#include "ZuluIDE.h"
#include <string.h>
#include <strings.h>

static const uint8_t DiscInformation[] =
{
    0x00,   //  0: disc info length, MSB
    0x20,   //  1: disc info length, LSB
    0x0E,   //  2: disc status (finalized, single session non-rewritable)
    0x01,   //  3: first track number
    0x01,   //  4: number of sessions (LSB)
    0x01,   //  5: first track in last session (LSB)
    0x01,   //  6: last track in last session (LSB)
    0x00,   //  7: format status (0x00 = non-rewritable, no barcode, no disc id)
    0x00,   //  8: disc type (0x00 = CD-ROM)
    0x00,   //  9: number of sessions (MSB)
    0x00,   // 10: first track in last session (MSB)
    0x00,   // 11: last track in last session (MSB)
    0x00,   // 12: disc ID (MSB)
    0x00,   // 13: .
    0x00,   // 14: .
    0x00,   // 15: disc ID (LSB)
    0x00,   // 16: last session lead-in start (MSB)
    0x00,   // 17: .
    0x00,   // 18: .
    0x00,   // 19: last session lead-in start (LSB)
    0x00,   // 20: last possible lead-out start (MSB)
    0x00,   // 21: .
    0x00,   // 22: .
    0x00,   // 23: last possible lead-out start (LSB)
    0x00,   // 24: disc bar code (MSB)
    0x00,   // 25: .
    0x00,   // 26: .
    0x00,   // 27: .
    0x00,   // 28: .
    0x00,   // 29: .
    0x00,   // 30: .
    0x00,   // 31: disc bar code (LSB)
    0x00,   // 32: disc application code
    0x00,   // 33: number of opc tables
};

static const uint8_t TrackInformation[] =
{
    0x00,   //  0: data length, MSB
    0x1A,   //  1: data length, LSB
    0x01,   //  2: track number
    0x01,   //  3: session number
    0x00,   //  4: reserved
    0x04,   //  5: track mode and flags
    0x8F,   //  6: data mode and flags
    0x00,   //  7: nwa_v
    0x00,   //  8: track start address (MSB)
    0x00,   //  9: .
    0x00,   // 10: .
    0x00,   // 11: track start address (LSB)
    0xFF,   // 12: next writable address (MSB)
    0xFF,   // 13: .
    0xFF,   // 14: .
    0xFF,   // 15: next writable address (LSB)
    0x00,   // 16: free blocks (MSB)
    0x00,   // 17: .
    0x00,   // 18: .
    0x00,   // 19: free blocks (LSB)
    0x00,   // 20: fixed packet size (MSB)
    0x00,   // 21: .
    0x00,   // 22: .
    0x00,   // 23: fixed packet size (LSB)
    0x00,   // 24: track size (MSB)
    0x00,   // 25: .
    0x00,   // 26: .
    0x00,   // 27: track size (LSB)
};

static const uint8_t SessionTOC[] =
{
    0x00, // toc length, MSB
    0x0A, // toc length, LSB
    0x01, // First session number
    0x01, // Last session number,
    // TRACK 1 Descriptor
    0x00, // reserved
    0x14, // Q sub-channel encodes current position, Digital track
    0x01, // First track number in last complete session
    0x00, // Reserved
    0x00,0x00,0x00,0x00 // LBA of first track in last session
};

static const uint8_t FullTOCHeader[] =
{
    0x00, //  0: toc length, MSB
    0x2E, //  1: toc length, LSB
    0x01, //  2: First session number
    0x01, //  3: Last session number,
    // A0 Descriptor
    0x01, //  4: session number
    0x14, //  5: ADR/Control
    0x00, //  6: TNO
    0xA0, //  7: POINT
    0x00, //  8: Min
    0x00, //  9: Sec
    0x00, // 10: Frame
    0x00, // 11: Zero
    0x01, // 12: First Track number.
    0x00, // 13: Disc type 00 = Mode 1
    0x00, // 14: PFRAME
    // A1
    0x01, // 15: session number
    0x14, // 16: ADR/Control
    0x00, // 17: TNO
    0xA1, // 18: POINT
    0x00, // 19: Min
    0x00, // 20: Sec
    0x00, // 21: Frame
    0x00, // 22: Zero
    0x01, // 23: Last Track number
    0x00, // 24: PSEC
    0x00, // 25: PFRAME
    // A2
    0x01, // 26: session number
    0x14, // 27: ADR/Control
    0x00, // 28: TNO
    0xA2, // 29: POINT
    0x00, // 30: Min
    0x00, // 31: Sec
    0x00, // 32: Frame
    0x00, // 33: Zero
    0x00, // 34: LEADOUT position
    0x00, // 35: leadout PSEC
    0x00, // 36: leadout PFRAME
};

// Convert logical block address to CD-ROM time
static void LBA2MSF(int32_t LBA, uint8_t* MSF, bool relative)
{
    if (!relative) {
        LBA += 150;
    }
    uint32_t ulba = LBA;
    if (LBA < 0) {
        ulba = LBA * -1;
    }

    MSF[2] = ulba % 75; // Frames
    uint32_t rem = ulba / 75;

    MSF[1] = rem % 60; // Seconds
    MSF[0] = rem / 60; // Minutes
}

// Convert logical block address to CD-ROM time in binary coded decimal format
static void LBA2MSFBCD(int32_t LBA, uint8_t* MSF, bool relative)
{
    LBA2MSF(LBA, MSF, relative);
    MSF[0] = ((MSF[0] / 10) << 4) | (MSF[0] % 10);
    MSF[1] = ((MSF[1] / 10) << 4) | (MSF[1] % 10);
    MSF[2] = ((MSF[2] / 10) << 4) | (MSF[2] % 10);
}

// Convert CD-ROM time to logical block address
static int32_t MSF2LBA(uint8_t m, uint8_t s, uint8_t f, bool relative)
{
    int32_t lba = (m * 60 + s) * 75 + f;
    if (!relative) lba -= 150;
    return lba;
}

// Format track info read from cue sheet into the format used by ReadTOC command.
// Refer to T10/1545-D MMC-4 Revision 5a, "Response Format 0000b: Formatted TOC"
static void formatTrackInfo(const CUETrackInfo *track, uint8_t *dest, bool use_MSF_time)
{
    uint8_t control_adr = 0x14; // Digital track

    if (track->track_mode == CUETrack_AUDIO)
    {
        control_adr = 0x10; // Audio track
    }

    dest[0] = 0; // Reserved
    dest[1] = control_adr;
    dest[2] = track->track_number;
    dest[3] = 0; // Reserved

    if (use_MSF_time)
    {
        // Time in minute-second-frame format
        dest[4] = 0;
        LBA2MSF(track->data_start, &dest[5], false);
    }
    else
    {
        // Time as logical block address
        dest[4] = (track->data_start >> 24) & 0xFF;
        dest[5] = (track->data_start >> 16) & 0xFF;
        dest[6] = (track->data_start >>  8) & 0xFF;
        dest[7] = (track->data_start >>  0) & 0xFF;
    }
}

// Format track info read from cue sheet into the format used by ReadFullTOC command.
// Refer to T10/1545-D MMC-4 Revision 5a, "Response Format 0010b: Raw TOC"
static void formatRawTrackInfo(const CUETrackInfo *track, uint8_t *dest, bool useBCD)
{
    uint8_t control_adr = 0x14; // Digital track

    if (track->track_mode == CUETrack_AUDIO)
    {
        control_adr = 0x10; // Audio track
    }

    dest[0] = 0x01; // Session always 1
    dest[1] = control_adr;
    dest[2] = 0x00; // "TNO", always 0?
    dest[3] = track->track_number; // "POINT", contains track number
    // Next three are ATIME. The spec doesn't directly address how these
    // should be reported in the TOC, just giving a description of Q-channel
    // data from Red Book/ECMA-130. On all disks tested so far these are
    // given as 00/00/00.
    dest[4] = 0x00;
    dest[5] = 0x00;
    dest[6] = 0x00;
    dest[7] = 0; // HOUR

    if (useBCD) {
        LBA2MSFBCD(track->data_start, &dest[8], false);
    } else {
        LBA2MSF(track->data_start, &dest[8], false);
    }
}

void IDECDROMDevice::initialize(int devidx)
{
    IDEATAPIDevice::initialize(devidx);

    m_devinfo.devtype = ATAPI_DEVTYPE_CDROM;
    m_devinfo.removable = true;
    m_devinfo.bytes_per_sector = 2048;
    strncpy(m_devinfo.ide_vendor, "ZuluIDE", sizeof(m_devinfo.ide_vendor));
    strncpy(m_devinfo.ide_product, "CD-ROM", sizeof(m_devinfo.ide_product));
    memcpy(m_devinfo.ide_revision, ZULU_FW_VERSION, sizeof(m_devinfo.ide_revision));
    strncpy(m_devinfo.atapi_model, "ZuluIDE CD-ROM", sizeof(m_devinfo.atapi_model));
    memcpy(m_devinfo.atapi_revision, ZULU_FW_VERSION, sizeof(m_devinfo.atapi_revision));
    m_devinfo.num_profiles = 1;
    m_devinfo.profiles[0] = ATAPI_PROFILE_CDROM;
    m_devinfo.current_profile = ATAPI_PROFILE_CDROM;
}

void IDECDROMDevice::set_image(IDEImage *image)
{
    IDEATAPIDevice::set_image(image);

    char filename[MAX_FILE_PATH];
    bool valid = false;

    if (image &&
        image->get_filename(filename, sizeof(filename)) &&
        strncasecmp(filename + strlen(filename) - 4, ".bin", 4) == 0)
    {
        char cuesheetname[MAX_FILE_PATH + 1] = {0};
        strncpy(cuesheetname, filename, strlen(filename) - 4);
        strlcat(cuesheetname, ".cue", sizeof(cuesheetname));

        valid = loadAndValidateCueSheet(cuesheetname);
    }

    if (!valid)
    {
        // No cue sheet or parsing failed, use as plain binary image.
        strcpy(m_cuesheet, R"(
            FILE "x" BINARY
            TRACK 01 MODE1/2048
            INDEX 01 00:00:00
        )");
        m_cueparser = CUEParser(m_cuesheet);
    }

    if (image)
    {
        CUETrackInfo firsttrack, lasttrack;
        getFirstLastTrackInfo(firsttrack, lasttrack);

        bool firstaudio = (firsttrack.track_mode == CUETrack_AUDIO);
        bool lastaudio = (lasttrack.track_mode == CUETrack_AUDIO);

        if (firstaudio && lastaudio)
        {
            m_devinfo.medium_type = ATAPI_MEDIUM_CDDA;
        }
        else if (!firstaudio && !lastaudio)
        {
            m_devinfo.medium_type = ATAPI_MEDIUM_CDROM;
        }
        else
        {
            m_devinfo.medium_type = ATAPI_MEDIUM_CDMIXED;
        }
    }
    else
    {
        m_devinfo.medium_type = ATAPI_MEDIUM_NONE;
    }

    // Notify host of media change
    m_atapi_state.unit_attention = true;

    if (!image)
    {
        m_devinfo.media_status_events = ATAPI_MEDIA_EVENT_EJECTREQ;
    }
    else
    {
        m_devinfo.media_status_events = ATAPI_MEDIA_EVENT_NEW;
    }
}

bool IDECDROMDevice::handle_atapi_command(const uint8_t *cmd)
{
    switch (cmd[0])
    {
        case ATAPI_CMD_SET_CD_SPEED:            return atapi_set_cd_speed(cmd);
        case ATAPI_CMD_READ_DISC_INFORMATION:   return atapi_read_disc_information(cmd);
        case ATAPI_CMD_READ_TRACK_INFORMATION:  return atapi_read_track_information(cmd);
        case ATAPI_CMD_READ_TOC:                return atapi_read_toc(cmd);
        case ATAPI_CMD_READ_HEADER:             return atapi_read_header(cmd);
        case ATAPI_CMD_READ_CD:                 return atapi_read_cd(cmd);
        case ATAPI_CMD_READ_CD_MSF:             return atapi_read_cd_msf(cmd);

        default:
            return IDEATAPIDevice::handle_atapi_command(cmd);
    }
}

bool IDECDROMDevice::atapi_set_cd_speed(const uint8_t *cmd)
{
    uint16_t read_speed = parse_be16(&cmd[2]);
    uint16_t write_speed = parse_be16(&cmd[4]);
    dbgmsg("-- Host requested read_speed=", (int)read_speed, ", write_speed=", (int)write_speed);
    return atapi_cmd_ok();
}

bool IDECDROMDevice::atapi_read_disc_information(const uint8_t *cmd)
{
    if (!m_image) return atapi_cmd_error(ATAPI_SENSE_NOT_READY, ATAPI_ASC_NO_MEDIUM);

    uint16_t allocationLength = parse_be16(&cmd[7]);

    // Take the hardcoded header as base
    uint8_t *buf = m_buffer.bytes;
    uint32_t len = sizeof(DiscInformation);
    memcpy(buf, DiscInformation, len);

    // Find first and last track number
    CUETrackInfo first, last;
    if (!getFirstLastTrackInfo(first, last))
    {
        logmsg("atapi_read_disc_information() failed to get track info");
        return atapi_cmd_error(ATAPI_SENSE_ABORTED_CMD, ATAPI_ASC_NO_MEDIUM);
    }

    buf[3] = first.track_number;
    buf[5] = first.track_number;
    buf[6] = last.track_number;

    atapi_send_data(buf, std::min<uint32_t>(allocationLength, len));
    return atapi_cmd_ok();
}

bool IDECDROMDevice::atapi_read_track_information(const uint8_t *cmd)
{
    if (!m_image) return atapi_cmd_error(ATAPI_SENSE_NOT_READY, ATAPI_ASC_NO_MEDIUM);

    bool track = (cmd[1] & 0x01);
    uint32_t lba = parse_be32(&cmd[2]);
    uint16_t allocationLength = parse_be16(&cmd[7]);

    // Take the hardcoded header as base
    uint8_t *buf = m_buffer.bytes;
    uint32_t len = sizeof(TrackInformation);
    memcpy(buf, TrackInformation, len);

    // Step through the tracks until the one requested is found
    // Result will be placed in mtrack for later use if found
    bool trackfound = false;
    uint32_t tracklen = 0;
    CUETrackInfo mtrack = {0};
    const CUETrackInfo *trackinfo;
    m_cueparser.restart();
    while ((trackinfo = m_cueparser.next_track()) != NULL)
    {
        if (mtrack.track_number != 0) // skip 1st track, just store later
        {
            if ((track && lba == mtrack.track_number)
                || (!track && lba < trackinfo->data_start))
            {
                trackfound = true;
                tracklen = trackinfo->data_start - mtrack.data_start;
                break;
            }
        }
        mtrack = *trackinfo;
    }
    // try the last track as a final attempt if no match found beforehand
    if (!trackfound)
    {
        uint32_t lastLba = getLeadOutLBA(&mtrack);
        if ((track && lba == mtrack.track_number)
            || (!track && lba < lastLba))
        {
            trackfound = true;
            tracklen = lastLba - mtrack.data_start;
        }
    }

    // bail out if no match found
    if (!trackfound)
    {
        return atapi_cmd_error(ATAPI_SENSE_ILLEGAL_REQ, ATAPI_ASC_INVALID_FIELD);
    }

    // rewrite relevant bytes, starting with track number
    buf[3] = mtrack.track_number;

    // track mode
    if (mtrack.track_mode == CUETrack_AUDIO)
    {
        buf[5] = 0x00;
    }

    // track start
    write_be32(&buf[8], mtrack.data_start);

    // track size
    write_be32(&buf[24], tracklen);

    dbgmsg("------ Reporting track ", mtrack.track_number, ", start ", mtrack.data_start,
            ", length ", tracklen);

    atapi_send_data(buf, std::min<uint32_t>(allocationLength, len));
    return atapi_cmd_ok();
}

bool IDECDROMDevice::atapi_read_toc(const uint8_t *cmd)
{
    if (!m_image) return atapi_cmd_error(ATAPI_SENSE_NOT_READY, ATAPI_ASC_NO_MEDIUM);

    bool MSF = (cmd[1] & 0x02);
    uint8_t track = cmd[6];
    uint16_t allocationLength = parse_be16(&cmd[7]);

    // The "format" field is reserved for SCSI-2
    uint8_t format = cmd[2] & 0x0F;

    // Matshita SCSI-2 drives appear to use the high 2 bits of the CDB
    // control byte to switch on session info (0x40) and full toc (0x80)
    // responses that are very similar to the standard formats described
    // in MMC-1. These vendor flags must have been pretty common because
    // even a modern SATA drive (ASUS DRW-24B1ST j) responds to them
    // (though it always replies in hex rather than bcd)
    //
    // The session information page is identical to MMC. The full TOC page
    // is identical _except_ it returns addresses in bcd rather than hex.
    bool useBCD = false;
    if (format == 0 && cmd[9] == 0x80)
    {
        format = 2;
        useBCD = true;
    }
    else if (format == 0 && cmd[9] == 0x40)
    {
        format = 1;
    }

    switch (format)
    {
        case 0: return doReadTOC(MSF, track, allocationLength); break; // SCSI-2
        case 1: return doReadSessionInfo(MSF, allocationLength); break; // MMC2
        case 2: return doReadFullTOC(track, allocationLength, useBCD); break; // MMC2
        default: return atapi_cmd_error(ATAPI_SENSE_ILLEGAL_REQ, ATAPI_ASC_INVALID_FIELD);
    }
}

// SCSI-3 MMC Read Header command, seems to be deprecated in later standards.
// Refer to ANSI X3.304-1997
// The spec is vague, but based on experimentation with a Matshita drive this
// command should return the sector header absolute time (see ECMA-130 21).
// Given 2048-byte block sizes this effectively is 1:1 with the provided LBA.
bool IDECDROMDevice::atapi_read_header(const uint8_t *cmd)
{
    if (!m_image) return atapi_cmd_error(ATAPI_SENSE_NOT_READY, ATAPI_ASC_NO_MEDIUM);

    bool MSF = (cmd[1] & 0x02);
    uint32_t lba = 0; // IGNORED for now
    uint16_t allocationLength = parse_be16(&cmd[7]);

    // Track mode (audio / data)
    CUETrackInfo trackinfo = getTrackFromLBA(lba);
    uint8_t mode = (trackinfo.track_mode == CUETrack_AUDIO) ? 0 : 1;

    uint8_t *buf = m_buffer.bytes;
    buf[0] = mode;
    buf[1] = 0; // reserved
    buf[2] = 0; // reserved
    buf[3] = 0; // reserved

    // Track start
    if (MSF)
    {
        buf[4] = 0;
        LBA2MSF(lba, &buf[5], false);
    }
    else
    {
        write_be32(&buf[4], lba);
    }

    uint8_t len = 8;
    atapi_send_data(buf, std::min<uint32_t>(allocationLength, len));
    return atapi_cmd_ok();
}

bool IDECDROMDevice::atapi_read_cd(const uint8_t *cmd)
{
    if (!m_image) return atapi_cmd_error(ATAPI_SENSE_NOT_READY, ATAPI_ASC_NO_MEDIUM);

    uint8_t sector_type = (cmd[1] >> 2) & 7;
    uint32_t lba = parse_be32(&cmd[2]);
    uint32_t blocks = parse_be24(&cmd[6]);
    uint8_t main_channel = cmd[9];
    uint8_t sub_channel = cmd[10];

    return doReadCD(lba, blocks, sector_type, main_channel, sub_channel, false);
}

bool IDECDROMDevice::atapi_read_cd_msf(const uint8_t *cmd)
{
    if (!m_image) return atapi_cmd_error(ATAPI_SENSE_NOT_READY, ATAPI_ASC_NO_MEDIUM);

    uint8_t sector_type = (cmd[1] >> 2) & 7;
    uint32_t start = MSF2LBA(cmd[3], cmd[4], cmd[5], false);
    uint32_t end   = MSF2LBA(cmd[6], cmd[7], cmd[8], false);
    uint8_t main_channel = cmd[9];
    uint8_t sub_channel = cmd[10];
    return doReadCD(start, end - start, sector_type, main_channel, sub_channel, false);
}

bool IDECDROMDevice::doReadTOC(bool MSF, uint8_t track, uint16_t allocationLength)
{
    uint8_t *buf = m_buffer.bytes;

    // Format track info
    uint8_t *trackdata = &buf[4];
    int trackcount = 0;
    int firsttrack = -1;
    CUETrackInfo lasttrack = {0};
    const CUETrackInfo *trackinfo;
    m_cueparser.restart();
    while ((trackinfo = m_cueparser.next_track()) != NULL)
    {
        if (firsttrack < 0) firsttrack = trackinfo->track_number;
        lasttrack = *trackinfo;

        if (track <= trackinfo->track_number)
        {
            formatTrackInfo(trackinfo, &trackdata[8 * trackcount], MSF);
            trackcount += 1;
        }
    }

    // Format lead-out track info
    CUETrackInfo leadout = {};
    leadout.track_number = 0xAA;
    leadout.track_mode = (lasttrack.track_number != 0) ? lasttrack.track_mode : CUETrack_MODE1_2048;
    leadout.data_start = getLeadOutLBA(&lasttrack);
    formatTrackInfo(&leadout, &trackdata[8 * trackcount], MSF);
    trackcount += 1;

    // Format response header
    uint16_t toc_length = 2 + trackcount * 8;
    buf[0] = toc_length >> 8;
    buf[1] = toc_length & 0xFF;
    buf[2] = firsttrack;
    buf[3] = lasttrack.track_number;

    if (track != 0xAA && trackcount < 2)
    {
        // Unknown track requested
        return atapi_cmd_error(ATAPI_SENSE_ILLEGAL_REQ, ATAPI_ASC_INVALID_FIELD);
    }
    else
    {
        uint32_t len = 2 + toc_length;
        atapi_send_data(buf, std::min<uint32_t>(allocationLength, len));
        return atapi_cmd_ok();
    }
}

bool IDECDROMDevice::doReadSessionInfo(bool MSF, uint16_t allocationLength)
{
    uint32_t len = sizeof(SessionTOC);
    uint8_t *buf = m_buffer.bytes;
    memcpy(buf, SessionTOC, len);

    // Replace first track info in the session table
    // based on data from CUE sheet.
    m_cueparser.restart();
    const CUETrackInfo *trackinfo = m_cueparser.next_track();
    if (trackinfo)
    {
        formatTrackInfo(trackinfo, &buf[4], false);
    }

    atapi_send_data(buf, std::min<uint32_t>(allocationLength, len));
    return atapi_cmd_ok();
}

bool IDECDROMDevice::doReadFullTOC(uint8_t session, uint16_t allocationLength, bool useBCD)
{
    // We only support session 1.
    if (session > 1)
    {
        return atapi_cmd_error(ATAPI_SENSE_ILLEGAL_REQ, ATAPI_ASC_INVALID_FIELD);
    }

    // Take basic header as the base
    uint8_t *buf = m_buffer.bytes;
    uint32_t len = sizeof(FullTOCHeader);
    memcpy(buf, FullTOCHeader, len);

    // Add track descriptors
    int trackcount = 0;
    int firsttrack = -1;
    CUETrackInfo lasttrack = {0};
    const CUETrackInfo *trackinfo;
    m_cueparser.restart();
    while ((trackinfo = m_cueparser.next_track()) != NULL)
    {
        if (firsttrack < 0)
        {
            firsttrack = trackinfo->track_number;
            if (trackinfo->track_mode == CUETrack_AUDIO)
            {
                buf[5] = 0x10;
            }
        }
        lasttrack = *trackinfo;

        formatRawTrackInfo(trackinfo, &buf[len], useBCD);
        trackcount += 1;
        len += 11;
    }

    // First and last track numbers
    buf[12] = firsttrack;
    if (lasttrack.track_number != 0)
    {
        buf[23] = lasttrack.track_number;
        if (lasttrack.track_mode == CUETrack_AUDIO)
        {
            buf[16] = 0x10;
            buf[27] = 0x10;
        }
    }

    // Leadout track position
    if (useBCD) {
        LBA2MSFBCD(getLeadOutLBA(&lasttrack), &buf[34], false);
    } else {
        LBA2MSF(getLeadOutLBA(&lasttrack), &buf[34], false);
    }

    // Correct the record length in header
    uint16_t toclen = len - 2;
    buf[0] = toclen >> 8;
    buf[1] = toclen & 0xFF;

    atapi_send_data(buf, std::min<uint32_t>(allocationLength, len));
    return atapi_cmd_ok();
}

bool IDECDROMDevice::doRead(uint32_t lba, uint32_t transfer_len)
{
    // Override IDEATAPIDevice::doRead() in case the CD image uses different sector length
    return doReadCD(lba, transfer_len, 0, 0x10, 0, true);
}

bool IDECDROMDevice::doReadCD(uint32_t lba, uint32_t length, uint8_t sector_type,
                              uint8_t main_channel, uint8_t sub_channel, bool data_only)
{
    CUETrackInfo trackinfo = getTrackFromLBA(lba);

    if (!m_image)
    {
        return atapi_cmd_error(ATAPI_SENSE_NOT_READY, ATAPI_ASC_NO_MEDIUM);
    }

    // Figure out the data offset in the file
    uint64_t offset = trackinfo.file_offset + trackinfo.sector_length * (lba - trackinfo.track_start);
    dbgmsg("---- Read CD: ", (int)length, " sectors starting at ", (int)lba,
           ", track number ", trackinfo.track_number, ", sector size ", (int)trackinfo.sector_length,
           ", main channel ", main_channel, ", sub channel ", sub_channel,
           ", data offset in file ", (int)offset);

    // Ensure read is not out of range of the image
    uint64_t readend = offset + trackinfo.sector_length * length;
    uint64_t capacity = m_image->capacity();
    if (readend > capacity)
    {
        logmsg("WARNING: Host attempted CD read at sector ", lba, "+", length,
              ", exceeding image size ", capacity);
        return atapi_cmd_error(ATAPI_SENSE_ILLEGAL_REQ, ATAPI_ASC_LBA_OUT_OF_RANGE);
    }

    // Verify sector type
    if (sector_type != 0)
    {
        bool sector_type_ok = false;
        if (sector_type == 1 && trackinfo.track_mode == CUETrack_AUDIO)
        {
            sector_type_ok = true;
        }
        else if (sector_type == 2 && trackinfo.track_mode == CUETrack_MODE1_2048)
        {
            sector_type_ok = true;
        }

        if (!sector_type_ok)
        {
            dbgmsg("---- Failed sector type check, host requested ", (int)sector_type, " CUE file has ", (int)trackinfo.track_mode);
            return atapi_cmd_error(ATAPI_SENSE_ILLEGAL_REQ, ATAPI_ASC_ILLEGAL_MODE_FOR_TRACK);
        }
    }

    // Select fields to transfer
    // Refer to table 351 in T10/1545-D MMC-4 Revision 5a
    // Only the mandatory cases are supported.
    m_cd_read_format.sector_length_file = 2048;
    m_cd_read_format.sector_length_out = 2048;
    m_cd_read_format.sector_data_skip = 0;
    m_cd_read_format.sector_data_length = 2048;
    m_cd_read_format.add_fake_headers = false;
    m_cd_read_format.field_q_subchannel = false;
    m_cd_read_format.start_lba = lba;
    m_cd_read_format.sectors_done = 0;

    if (main_channel == 0)
    {
        // No actual data requested, just sector type check or subchannel
        m_cd_read_format.sector_length_file = 0;
        m_cd_read_format.sector_data_length = 0;
    }
    else if (trackinfo.track_mode == CUETrack_AUDIO)
    {
        // Transfer whole 2352 byte audio sectors from file to host
        m_cd_read_format.sector_length_file = 2352;
        m_cd_read_format.sector_data_length = 2352;
        m_cd_read_format.sector_length_out = 2352;
    }
    else if (trackinfo.track_mode == CUETrack_MODE1_2048 && main_channel == 0x10)
    {
        // Transfer whole 2048 byte data sectors from file to host
        m_cd_read_format.sector_length_file = 2048;
        m_cd_read_format.sector_data_length = 2048;
        m_cd_read_format.sector_length_out = 2048;
    }
    else if (trackinfo.track_mode == CUETrack_MODE1_2048 && (main_channel & 0xB8) == 0xB8)
    {
        // Transfer 2048 bytes of data from file and fake the headers
        m_cd_read_format.sector_length_file = 2048;
        m_cd_read_format.sector_data_length = 2048;
        m_cd_read_format.sector_length_out = 2048 + 304;
        m_cd_read_format.add_fake_headers = true;
        dbgmsg("------ Host requested ECC data but image file lacks it, replacing with zeros");
    }
    else if (trackinfo.track_mode == CUETrack_MODE1_2352 && main_channel == 0x10)
    {
        // Transfer the 2048 byte payload of data sector to host.
        m_cd_read_format.sector_length_file = 2352;
        m_cd_read_format.sector_data_skip = 16;
        m_cd_read_format.sector_data_length = 2048;
        m_cd_read_format.sector_length_out = 2048;
    }
    else if (trackinfo.track_mode == CUETrack_MODE1_2352 && (main_channel & 0xB8) == 0xB8)
    {
        // Transfer whole 2352 byte data sector with ECC to host
        m_cd_read_format.sector_length_file = 2352;
        m_cd_read_format.sector_data_length = 2352;
        m_cd_read_format.sector_length_out = 2352;
    }
    else
    {
        dbgmsg("---- Unsupported channel request for track type ", (int)trackinfo.track_mode);
        return atapi_cmd_error(ATAPI_SENSE_ILLEGAL_REQ, ATAPI_ASC_ILLEGAL_MODE_FOR_TRACK);
    }

    if (data_only && m_cd_read_format.sector_length_out != 2048)
    {
        dbgmsg("------ Host tried to read non-data sector with standard READ command");
        return atapi_cmd_error(ATAPI_SENSE_ILLEGAL_REQ, ATAPI_ASC_ILLEGAL_MODE_FOR_TRACK);
    }

    if (sub_channel == 2)
    {
        // Include position information in Q subchannel
        m_cd_read_format.field_q_subchannel = true;
        m_cd_read_format.sector_length_out += 16;
    }
    else if (sub_channel != 0)
    {
        dbgmsg("---- Unsupported subchannel request");
        return atapi_cmd_error(ATAPI_SENSE_ILLEGAL_REQ, ATAPI_ASC_INVALID_FIELD);
    }

    if (m_cd_read_format.sector_length_file == 0)
    {
        // No actual data needed, just send headers
        read_callback(nullptr, 0, length);
        return atapi_send_wait_finish() && atapi_cmd_ok();
    }
    else if (m_image->read(offset, m_cd_read_format.sector_length_file, length, this))
    {
        return atapi_send_wait_finish() && atapi_cmd_ok();
    }
    else
    {
        dbgmsg("-- CD read failed, starting offset ", (int)offset, " length ", (int)length);
        return atapi_cmd_error(ATAPI_SENSE_MEDIUM_ERROR, 0);
    }
}

ssize_t IDECDROMDevice::read_callback(const uint8_t *data, size_t blocksize, size_t num_blocks)
{
    if (m_cd_read_format.sector_length_file == m_cd_read_format.sector_length_out)
    {
        // Simple case, send data directly
        atapi_send_data(data, blocksize, num_blocks, false);
        return num_blocks;
    }

    // Reformat sector data for transmission
    assert(sizeof(m_buffer) >= m_cd_read_format.sector_length_out);
    for (size_t i = 0; i < num_blocks; i++)
    {
        uint8_t *buf = m_buffer.bytes;
        uint32_t current_lba = m_cd_read_format.start_lba + m_cd_read_format.sectors_done;

        if (m_cd_read_format.add_fake_headers)
        {
            // 12-byte data sector sync pattern
            *buf++ = 0x00;
            for (int i = 0; i < 10; i++)
            {
                *buf++ = 0xFF;
            }
            *buf++ = 0x00;

            // 4-byte data sector header
            LBA2MSFBCD(current_lba, buf, false);
            buf += 3;
            *buf++ = 0x01; // Mode 1
        }

        if (m_cd_read_format.sector_data_length > 0)
        {
            const uint8_t *data_start = data + blocksize * i + m_cd_read_format.sector_data_skip;
            size_t data_length = m_cd_read_format.sector_data_length;
            memcpy(buf, data_start, data_length);
            buf += data_length;
        }

        if (m_cd_read_format.add_fake_headers)
        {
            // 288 bytes of ECC
            memset(buf, 0, 288);
            buf += 288;
        }

        if (m_cd_read_format.field_q_subchannel)
        {
            // Formatted Q subchannel data
            // Refer to table 354 in T10/1545-D MMC-4 Revision 5a
            // and ECMA-130 22.3.3
            *buf++ = (m_cd_read_format.trackinfo.track_mode == CUETrack_AUDIO ? 0x10 : 0x14); // Control & ADR
            *buf++ = m_cd_read_format.trackinfo.track_number;
            *buf++ = (current_lba >= m_cd_read_format.trackinfo.data_start) ? 1 : 0; // Index number (0 = pregap)
            int32_t rel = (int32_t)(current_lba) - (int32_t)m_cd_read_format.trackinfo.data_start;
            LBA2MSF(rel, buf, true); buf += 3;
            *buf++ = 0;
            LBA2MSF(current_lba, buf, false); buf += 3;
            *buf++ = 0; *buf++ = 0; // CRC (optional)
            *buf++ = 0; *buf++ = 0; *buf++ = 0; // (pad)
            *buf++ = 0; // No P subchannel
        }

        assert(buf == m_buffer.bytes + m_cd_read_format.sector_length_out);
        if (!atapi_send_data(m_buffer.bytes, m_cd_read_format.sector_length_out, 1, false))
        {
            dbgmsg("-- IDECDROMDevice atapi_send_data failed, length ", (int)m_cd_read_format.sector_length_out);
            return -1;
        }
        m_cd_read_format.sectors_done += 1;
    }

    return num_blocks;
}

bool IDECDROMDevice::loadAndValidateCueSheet(const char *cuesheetname)
{
    FsFile cuesheetfile = SD.open(cuesheetname, O_RDONLY);
    if (!cuesheetfile.isOpen())
    {
        logmsg("---- No CUE sheet found at ", cuesheetname, ", using as plain binary image");
        return false;
    }

    if (cuesheetfile.size() > sizeof(m_cuesheet))
    {
        logmsg("---- WARNING: CUE sheet length ", (int)cuesheetfile.size(), " exceeds maximum ",
                (int)sizeof(m_cuesheet), " bytes");
    }

    int len = cuesheetfile.read(m_cuesheet, sizeof(m_cuesheet));
    cuesheetfile.close();
    if (len <= 0)
    {
        m_cuesheet[0] = 0;
        logmsg("---- Failed to read cue sheet from ", cuesheetname);
        return false;
    }

    m_cuesheet[len] = 0;
    m_cueparser = CUEParser(m_cuesheet);

    const CUETrackInfo *trackinfo;
    int trackcount = 0;
    while ((trackinfo = m_cueparser.next_track()) != NULL)
    {
        trackcount++;

        if (trackinfo->track_mode != CUETrack_AUDIO &&
            trackinfo->track_mode != CUETrack_MODE1_2048 &&
            trackinfo->track_mode != CUETrack_MODE1_2352)
        {
            logmsg("---- Warning: track ", trackinfo->track_number, " has unsupported mode ", (int)trackinfo->track_mode);
        }

        if (trackinfo->file_mode != CUEFile_BINARY)
        {
            logmsg("---- Unsupported CUE data file mode ", (int)trackinfo->file_mode);
        }
    }

    if (trackcount == 0)
    {
        logmsg("---- Opened cue sheet ", cuesheetname, " but no valid tracks found");
        return false;
    }

    logmsg("---- Cue sheet ", cuesheetname, " loaded with ", (int)trackcount, " tracks");
    return true;
}

bool IDECDROMDevice::getFirstLastTrackInfo(CUETrackInfo &first, CUETrackInfo &last)
{
    m_cueparser.restart();

    const CUETrackInfo *trackinfo;
    bool got_track = false;
    while ((trackinfo = m_cueparser.next_track()) != NULL)
    {
        if (!got_track)
        {
            first = *trackinfo;
            got_track = true;
        }

        last = *trackinfo;
    }

    return got_track;
}

uint64_t IDECDROMDevice::capacity_lba()
{
    if (!m_image) return 0;

    CUETrackInfo first, last;
    getFirstLastTrackInfo(first, last);
    return (uint64_t)getLeadOutLBA(&last);
}

uint32_t IDECDROMDevice::getLeadOutLBA(const CUETrackInfo* lasttrack)
{
    if (lasttrack != nullptr && lasttrack->track_number != 0 && m_image != nullptr)
    {
        uint32_t lastTrackBlocks = (m_image->capacity() - lasttrack->file_offset) / lasttrack->sector_length;
        return lasttrack->track_start + lastTrackBlocks;
    }
    else
    {
        return 1;
    }
}

// Fetch track info based on LBA
CUETrackInfo IDECDROMDevice::getTrackFromLBA(uint32_t lba)
{
    CUETrackInfo result = {};
    const CUETrackInfo *tmptrack;
    m_cueparser.restart();
    while ((tmptrack = m_cueparser.next_track()) != NULL)
    {
        if (tmptrack->track_start <= lba)
        {
            result = *tmptrack;
        }
        else
        {
            break;
        }
    }

    return result;
}

size_t IDECDROMDevice::atapi_get_configuration(uint16_t feature, uint8_t *buffer, size_t max_bytes)
{
    if (feature == ATAPI_FEATURE_CDREAD)
    {
        write_be16(&buffer[0], feature);
        buffer[2] = 0x0B;
        buffer[3] = 4;
        buffer[4] = 0; // CD-ROM extra features not supported
        buffer[5] = 0;
        buffer[6] = 0;
        buffer[7] = 0;
        return 8;
    }

    return IDEATAPIDevice::atapi_get_configuration(feature, buffer, max_bytes);
}

size_t IDECDROMDevice::atapi_get_mode_page(uint8_t page_ctrl, uint8_t page_idx, uint8_t *buffer, size_t max_bytes)
{
    if (page_idx == ATAPI_MODESENSE_CDROM)
    {
        buffer[0] = ATAPI_MODESENSE_CDROM; // Page idx
        buffer[1] = 0x06; // Page length
        buffer[2] = 0; // Reserved
        buffer[3] = 7; // Inactivity time
        buffer[4] = 0;
        buffer[5] = 60; // Seconds per minute
        buffer[6] = 0;
        buffer[7] = 75; // Frames per second

        if (page_ctrl == 1)
        {
            // Mask out unchangeable parameters
            memset(buffer + 2, 0, 6);
        }

        return 8;
    }

    return IDEATAPIDevice::atapi_get_mode_page(page_ctrl, page_idx, buffer, max_bytes);
}