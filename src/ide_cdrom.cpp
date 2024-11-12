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
#include <minIni.h>
#include <zuluide/images/image_iterator.h>
#include <status/status_controller.h>
#ifdef ENABLE_AUDIO_OUTPUT
#include "ZuluIDE_audio.h"
#endif

extern zuluide::status::StatusController g_StatusController;

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

    set_inquiry_strings("ZuluIDE", "ZuluIDE CD-ROM", "1.0");
    set_ident_strings("ZuluIDE CD-ROM", "1234567890", "1.0");

    m_devinfo.num_profiles = 1;
    m_devinfo.profiles[0] = ATAPI_PROFILE_CDROM;
    m_devinfo.current_profile = ATAPI_PROFILE_CDROM;

    set_esn_event(esn_event_t::NoChange);
#if ENABLE_AUDIO_OUTPUT
#endif // ENABLE_AUDIO_OUTPUT
}

void IDECDROMDevice::reset() 
{
    IDEATAPIDevice::reset();
    set_esn_event(esn_event_t::NoChange);
}

void IDECDROMDevice::set_image(IDEImage *image)
{
    char filename[MAX_FILE_PATH];
    bool valid = false;

    IDEATAPIDevice::set_image(image);
    m_selected_file_index = -1;

    if (image &&
        !image->is_folder() &&
        image->get_filename(filename, sizeof(filename)) &&
        strncasecmp(filename + strlen(filename) - 4, ".bin", 4) == 0)
    {
        // There is a cue sheet with the same name as the .bin
        char cuesheetname[MAX_FILE_PATH + 1] = {0};
        strncpy(cuesheetname, filename, strlen(filename) - 4);
        strlcat(cuesheetname, ".cue", sizeof(cuesheetname));

        IDEImageFile *imagefile = (IDEImageFile*)image;
        valid = loadAndValidateCueSheet(imagefile->get_folder(), cuesheetname);
#ifdef ENABLE_AUDIO_OUTPUT
    if (valid)
        audio_set_cue_parser(&m_cueparser, imagefile->get_file());
#endif // ENABLE_AUDIO_OUTPUT
    }
    else if (image && image->is_folder())
    {
        // This is a folder, find the first valid cue sheet file
        char foldername[MAX_FILE_PATH + 1] = {0};
        image->get_foldername(foldername, sizeof(foldername));

        IDEImageFile *imagefile = (IDEImageFile*)image;
        FsFile *folder = imagefile->get_folder();
        FsFile iterfile;
        valid = false;
        filename[0] = '\0';
        while (!valid && iterfile.openNext(folder, O_RDONLY))
        {
            iterfile.getName(filename, sizeof(filename));
            if (strncasecmp(filename + strlen(filename) - 4, ".cue", 4) == 0)
            {
                valid = loadAndValidateCueSheet(folder, filename);
            }
        }

        if (!valid)
        {
            logmsg("No valid .cue sheet found in folder '", foldername, "'");
            image = nullptr;
        }
#ifdef ENABLE_AUDIO_OUTPUT
        else 
            audio_set_cue_parser(&m_cueparser, folder);
#endif // ENABLE_AUDIO_OUTPUT
    }

    if (!valid)
    {
        // No cue sheet, use as plain binary image.
        strcpy(m_cuesheet, R"(
            FILE "" BINARY
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
        // Notify host of media change
        m_atapi_state.unit_attention = true;
        m_atapi_state.sense_key = ATAPI_SENSE_NOT_READY;
        m_atapi_state.sense_asc = ATAPI_ASC_NO_MEDIUM;
    }


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
        case ATAPI_CMD_READ_SUB_CHANNEL:        return atapi_read_sub_channel(cmd);
        case ATAPI_CMD_READ_TOC:                return atapi_read_toc(cmd);
        case ATAPI_CMD_READ_HEADER:             return atapi_read_header(cmd);
        case ATAPI_CMD_READ_CD:                 return atapi_read_cd(cmd);
        case ATAPI_CMD_READ_CD_MSF:             return atapi_read_cd_msf(cmd);
        case ATAPI_CMD_GET_EVENT_STATUS_NOTIFICATION: return atapi_get_event_status_notification(cmd);
        case ATAPI_CMD_PLAY_AUDIO_10:           return atapi_play_audio_10(cmd);
        case ATAPI_CMD_PLAY_AUDIO_12:           return atapi_play_audio_12(cmd);
        case ATAPI_CMD_PLAY_AUDIO_MSF:          return atapi_play_audio_msf(cmd);
        case ATAPI_CMD_PAUSE_RESUME_AUDIO:      return atapi_pause_resume_audio(cmd);
        case ATAPI_CMD_STOP_PLAY_SCAN_AUDIO:    return atapi_stop_play_scan_audio(cmd);
        case ATAPI_CMD_SEEK10:                  return atapi_seek_10(cmd);

        default:
            return IDEATAPIDevice::handle_atapi_command(cmd);
    }
}

bool IDECDROMDevice::atapi_cmd_not_ready_error()
{
    if (m_removable.ejected)
        return atapi_cmd_error(ATAPI_SENSE_NOT_READY, ATAPI_ASC_NO_MEDIUM_TRAY_OPEN);
    return atapi_cmd_error(ATAPI_SENSE_NOT_READY, ATAPI_ASC_NO_MEDIUM);
}

bool IDECDROMDevice::atapi_set_cd_speed(const uint8_t *cmd)
{
#if ENABLE_AUDIO_OUTPUT
    // terminate audio playback if active on this target (Annex C)
    audio_stop();
#endif
    uint16_t read_speed = parse_be16(&cmd[2]);
    uint16_t write_speed = parse_be16(&cmd[4]);
    dbgmsg("-- Host requested read_speed=", (int)read_speed, ", write_speed=", (int)write_speed);
    return atapi_cmd_ok();
}

bool IDECDROMDevice::atapi_read_disc_information(const uint8_t *cmd)
{
#if ENABLE_AUDIO_OUTPUT
    // terminate audio playback if active on this target (Annex C)
    audio_stop();
#endif

    if (!is_medium_present()) return atapi_cmd_not_ready_error();
    if (m_atapi_state.not_ready) return atapi_cmd_error(ATAPI_SENSE_NOT_READY, ATAPI_ASC_UNIT_BECOMING_READY);

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
#if ENABLE_AUDIO_OUTPUT
    // terminate audio playback if active on this target (Annex C)
    audio_stop();
#endif

    if (!is_medium_present()) return atapi_cmd_not_ready_error();
    if (m_atapi_state.not_ready) return atapi_cmd_error(ATAPI_SENSE_NOT_READY, ATAPI_ASC_UNIT_BECOMING_READY);

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
    uint64_t prev_capacity = 0;
    m_cueparser.restart();
    while ((trackinfo = m_cueparser.next_track(prev_capacity)) != NULL)
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

        selectBinFileForTrack(trackinfo);
        prev_capacity = m_image->capacity();
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

bool IDECDROMDevice::atapi_read_sub_channel(const uint8_t * cmd)
{
    if (!is_medium_present()) return atapi_cmd_not_ready_error();
    if (m_atapi_state.not_ready) return atapi_cmd_error(ATAPI_SENSE_NOT_READY, ATAPI_ASC_UNIT_BECOMING_READY);

    bool time = (cmd[1] & 0x02);
    bool subq = (cmd[2] & 0x40);
    uint8_t parameter = cmd[3];
    uint8_t track_number = cmd[6];
    uint16_t allocationLength = (((uint32_t) cmd[7]) << 8) + cmd[8];

    return doReadSubChannel(time, subq, parameter, track_number, allocationLength);
}

bool IDECDROMDevice::atapi_read_toc(const uint8_t *cmd)
{
    if (!is_medium_present()) return atapi_cmd_not_ready_error();
    if (m_atapi_state.not_ready) return atapi_cmd_error(ATAPI_SENSE_NOT_READY, ATAPI_ASC_UNIT_BECOMING_READY);

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
#if ENABLE_AUDIO_OUTPUT
    // terminate audio playback if active on this target (Annex C)
    audio_stop();
#endif

    if (!is_medium_present()) return atapi_cmd_not_ready_error();
    if (m_atapi_state.not_ready) return atapi_cmd_error(ATAPI_SENSE_NOT_READY, ATAPI_ASC_UNIT_BECOMING_READY);

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
#if ENABLE_AUDIO_OUTPUT
    // terminate audio playback if active on this target (Annex C)
    audio_stop();
#endif
    if (!is_medium_present()) return atapi_cmd_not_ready_error();
    if (m_atapi_state.not_ready) return atapi_cmd_error(ATAPI_SENSE_NOT_READY, ATAPI_ASC_UNIT_BECOMING_READY);

    uint8_t sector_type = (cmd[1] >> 2) & 7;
    uint32_t lba = parse_be32(&cmd[2]);
    uint32_t blocks = parse_be24(&cmd[6]);
    uint8_t main_channel = cmd[9];
    uint8_t sub_channel = cmd[10];

    return doReadCD(lba, blocks, sector_type, main_channel, sub_channel, false);
}

bool IDECDROMDevice::atapi_read_cd_msf(const uint8_t *cmd)
{
#if ENABLE_AUDIO_OUTPUT
    // terminate audio playback if active on this target (Annex C)
    audio_stop();
#endif
    if (!is_medium_present()) return atapi_cmd_not_ready_error();
    if (m_atapi_state.not_ready) return atapi_cmd_error(ATAPI_SENSE_NOT_READY, ATAPI_ASC_UNIT_BECOMING_READY);

    uint8_t sector_type = (cmd[1] >> 2) & 7;
    uint32_t start = MSF2LBA(cmd[3], cmd[4], cmd[5], false);
    uint32_t end   = MSF2LBA(cmd[6], cmd[7], cmd[8], false);
    uint8_t main_channel = cmd[9];
    uint8_t sub_channel = cmd[10];
    return doReadCD(start, end - start, sector_type, main_channel, sub_channel, false);
}

bool IDECDROMDevice::atapi_get_event_status_notification(const uint8_t *cmd)
{
    uint8_t *buf = m_buffer.bytes;
    if (!(cmd[1] & 1))
    {
        // Async. notification is not supported
        return atapi_cmd_error(ATAPI_SENSE_ILLEGAL_REQ, ATAPI_ASC_INVALID_FIELD);
    }
    // Operation change class request
    else if ((cmd[4] & 0x02) && (m_esn.event != esn_event_t::NoChange && m_esn.current_event == esn_event_t::NoChange))
    {
        buf[0] = 0;
        buf[1] = 6; // EventDataLength
        buf[2] = 0x01;   // Operational Change request/notification
        buf[3] = 0x12; // Supported events
        buf[4] = 0x02; // Operational state has changed
        buf[5] = 0x00; // Power status
        buf[6] = 0x00; // Start slot
        buf[7] = 0x01; // End slot 
        esn_next_event();
    }
    // Media class request
    else if (cmd[4] & 0x10)
    {
        if (m_esn.request == IDECDROMDevice::esn_class_request_t::Media && 
            (   
                m_esn.event == esn_event_t::MNewMedia
                || m_esn.event == esn_event_t::MEjectRequest
                || m_esn.event == esn_event_t::MMediaRemoval
            ))
        {
            // If the Event Status Notification was Operational Change request
            // was never issued, proceed to the next event
            if (m_esn.current_event == esn_event_t::NoChange)
                esn_next_event();

            if (m_esn.current_event == esn_event_t::MNewMedia || m_esn.current_event == esn_event_t::MEjectRequest)
            {
                // Report media status events
                buf[0] = 0;
                buf[1] = 6; // EventDataLength
                buf[2] = m_esn.request; // Media status events
                buf[3] = 0x12; // Supported events
                if (m_esn.current_event == esn_event_t::MEjectRequest) 
                    buf[4] = 0x01; // Eject Request
                else if (m_esn.current_event == esn_event_t::MNewMedia)
                    buf[4] = 0x02; // New Media
                else
                    return atapi_cmd_error(ATAPI_SENSE_ILLEGAL_REQ, ATAPI_ASC_INVALID_CMD);
                buf[5] = 0x02; // Media Present
                buf[6] = 0; // Start slot
                buf[7] = 0; // End slot
#if ENABLE_AUDIO_OUTPUT
                audio_stop();
#endif
                 esn_next_event();
            }
            else if (m_esn.current_event == esn_event_t::MMediaRemoval)
            {
                // Report media status event Media Removal
                buf[0] = 0;
                buf[1] = 6; // EventDataLength
                buf[2] = m_esn.request; // Media status events
                buf[3] = 0x12; // Supported events
                buf[4] = 0x03; // Media Removal
                buf[5] = 0x02; // Media Present
                buf[6] = 0; // Start slot
                buf[7] = 0; // End slot
                esn_next_event();
                eject_media();
            }
        }
        // output media no change
        else 
        {
            // Report media status events
            buf[0] = 0;
            buf[1] = 6; // EventDataLength
            buf[2] = IDECDROMDevice::esn_class_request_t::Media; // Media status events
            buf[3] = 0x12; // Supported events
            buf[4] = 0x00; // No Change
            if (m_removable.ejected)
                buf[5] = 0x00;
            else
                buf[5] = 0x02; // Media Present
            buf[6] = 0; // Start slot
            buf[7] = 0; // End slot
            set_esn_event(esn_event_t::NoChange);
        }
    }
    else
    {
        // No events to report
        buf[0] = 0;
        buf[1] = 0x06; // EventDataLength
        buf[2] = 0x01; // Operational Change request/notification
        buf[3] = 0x12; // Supported events
        buf[4] = 0x00; // No change to operational state
        buf[5] = 0x00; // Logical unit ready for operation
        buf[6] = 0x00; // Start slot
        buf[7] = 0x00; // End slot
        set_esn_event(esn_event_t::NoChange);
    }

    if (!atapi_send_data(m_buffer.bytes, 8))
    {
        return atapi_cmd_error(ATAPI_SENSE_ABORTED_CMD, 0);
    }
    return atapi_cmd_ok();
}

bool IDECDROMDevice::atapi_start_stop_unit(const uint8_t *cmd)
{
    if ((cmd[ATAPI_START_STOP_EJT_OFFSET] & ATAPI_START_STOP_START) == 0)
    {
        audio_stop();
    }
    return IDEATAPIDevice::atapi_start_stop_unit(cmd);
}

bool IDECDROMDevice::atapi_play_audio_10(const uint8_t *cmd)
{
    uint32_t lba = parse_be32(&cmd[2]);
    uint32_t blocks = parse_be32(&cmd[6]);

    return doPlayAudio(lba, blocks);
}

bool IDECDROMDevice::atapi_play_audio_12(const uint8_t *cmd)
{
    uint32_t lba = parse_be32(&cmd[2]);
    uint16_t blocks = parse_be16(&cmd[7]);

    return doPlayAudio(lba, blocks);

}

bool IDECDROMDevice::atapi_play_audio_msf(const uint8_t *cmd)
{
    uint8_t m = cmd[3];
    uint8_t s = cmd[4];
    uint8_t f = cmd[5];
    uint32_t start_lba = MSF2LBA(m, s, f, false);

#ifdef ENABLE_AUDIO_OUTPUT
    if (m == 0xFF && s == 0xFF && f == 0xFF)
    {
            // request to start playback from 'current position'
            start_lba = audio_get_lba_position();
    }
#endif

    m =  cmd[6];
    s =  cmd[7];
    f =  cmd[8];
    uint32_t stop_lba = MSF2LBA(m, s, f, false);

    uint32_t blocks = stop_lba - start_lba;

    return doPlayAudio(start_lba, blocks);
}


static void doStopAudio()
{
    dbgmsg("------ CD-ROM Stop Audio request");
#ifdef ENABLE_AUDIO_OUTPUT
    audio_stop();
#endif
}

bool IDECDROMDevice::atapi_stop_play_scan_audio(const uint8_t *cmd)
{
        // STOP PLAY/SCAN
        doStopAudio();
        return atapi_cmd_ok();
}

bool IDECDROMDevice::atapi_pause_resume_audio(const uint8_t *cmd)
{
#ifdef ENABLE_AUDIO_OUTPUT
    bool resume = cmd[8] & 1;
    dbgmsg("------ CD-ROM ", resume ? "resume" : "pause", " audio playback");
    if (audio_is_playing())
    {
        audio_set_paused(!resume);
        return atapi_cmd_ok();
    }
    else
    {
        return atapi_cmd_error(ATAPI_SENSE_ILLEGAL_REQ, ATAPI_ASC_COMMAND_SEQUENCE_ERROR);
    }
#else
    dbgmsg("---- Target does not support audio pausing");
    return atapi_cmd_error(ATAPI_SENSE_ILLEGAL_REQ, ATAPI_ASC_NO_ASC);
#endif
}

bool IDECDROMDevice::atapi_seek_10(const uint8_t *cmd)
{
    uint32_t lba = parse_be32(&cmd[2]);
    dbgmsg("---- Seek 10 - LBA: ", lba, " - seek not implemented - Win95 uses to pause audio ");
#ifdef ENABLE_AUDIO_OUTPUT
    doStopAudio();
#endif
    return atapi_cmd_ok();
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
    uint64_t prev_capacity = 0;
    m_cueparser.restart();
    while ((trackinfo = m_cueparser.next_track(prev_capacity)) != NULL)
    {
        if (firsttrack < 0) firsttrack = trackinfo->track_number;
        lasttrack = *trackinfo;

        if (track <= trackinfo->track_number)
        {
            formatTrackInfo(trackinfo, &trackdata[8 * trackcount], MSF);
            trackcount += 1;
        }

        selectBinFileForTrack(trackinfo);
        prev_capacity = m_image->capacity();
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
    uint64_t prev_capacity = 0;
    m_cueparser.restart();
    while ((trackinfo = m_cueparser.next_track(prev_capacity)) != NULL)
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

        selectBinFileForTrack(trackinfo);
        prev_capacity = m_image->capacity();
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


/**************************************/
/* CD-ROM audio playback              */
/**************************************/

void IDECDROMDevice::cdromGetAudioPlaybackStatus(uint8_t *status, uint32_t *current_lba, bool current_only)
{
#ifdef ENABLE_AUDIO_OUTPUT
    if (status) {
        if (current_only) {
            *status = audio_is_playing() ? 1 : 0;
        } else {
            *status = (uint8_t) audio_get_status_code();
        }
    }
    if (current_lba) *current_lba = audio_get_lba_position();
#else
    if (status) *status = 0; // audio status code for 'unsupported/invalid' and not-playing indicator
    if (current_lba) *current_lba = 0;
#endif

}

bool IDECDROMDevice::doReadSubChannel(bool time, bool subq, uint8_t parameter, uint8_t track_number, uint16_t allocation_length)
{
    uint8_t *buf = m_buffer.bytes;

    if (parameter == 0x01)
    {
        uint8_t audiostatus;
        uint32_t lba;
        cdromGetAudioPlaybackStatus(&audiostatus, &lba, false);
        dbgmsg("------ Get audio playback position: status ", (int)audiostatus, " lba ", (int)lba);

        // Fetch current track info
        CUETrackInfo trackinfo = getTrackFromLBA(lba);

        // Request sub channel data at current playback position
        *buf++ = 0; // Reserved
        *buf++ = audiostatus;

        int len;
        if (subq)
        {
            len = 12;
            *buf++ = 0;  // Subchannel data length (MSB)
            *buf++ = len; // Subchannel data length (LSB)
            *buf++ = 0x01; // Subchannel data format
            *buf++ = (trackinfo.track_mode == CUETrack_AUDIO ? 0x10 : 0x14);
            *buf++ = trackinfo.track_number;
            *buf++ = (lba >= trackinfo.data_start) ? 1 : 0; // Index number (0 = pregap)
            if (time)
            {
                *buf++ = 0;
                LBA2MSF(lba, buf, false);
                dbgmsg("------ ABS M ", *buf, " S ", *(buf+1), " F ", *(buf+2));
                buf += 3;
            }
            else
            {
                *buf++ = (lba >> 24) & 0xFF; // Absolute block address
                *buf++ = (lba >> 16) & 0xFF;
                *buf++ = (lba >>  8) & 0xFF;
                *buf++ = (lba >>  0) & 0xFF;
            }

            int32_t relpos = (int32_t)lba - (int32_t)trackinfo.data_start;
            if (time)
            {
                *buf++ = 0;
                LBA2MSF(relpos, buf, true);
                dbgmsg("------ REL M ", *buf, " S ", *(buf+1), " F ", *(buf+2));
                buf += 3;
            }
            else
            {
                uint32_t urelpos = relpos;
                *buf++ = (urelpos >> 24) & 0xFF; // Track relative position (may be negative)
                *buf++ = (urelpos >> 16) & 0xFF;
                *buf++ = (urelpos >>  8) & 0xFF;
                *buf++ = (urelpos >>  0) & 0xFF;
            }
        }
        else
        {
            len = 0;
            *buf++ = 0;
            *buf++ = 0;
        }
        len += 4;

        if (len > allocation_length) len = allocation_length;
        atapi_send_data(m_buffer.bytes, std::min<uint32_t>(allocation_length, len));
        return atapi_cmd_ok();
    }
    else
    {
        dbgmsg("---- Unsupported subchannel request");
        return atapi_cmd_error(ATAPI_SENSE_ILLEGAL_REQ, ATAPI_ASC_INVALID_FIELD);;
    }

}

bool IDECDROMDevice::doRead(uint32_t lba, uint32_t transfer_len)
{
    // Override IDEATAPIDevice::doRead() in case the CD image uses different sector length
    return doReadCD(lba, transfer_len, 0, 0x10, 0, true);
}

bool IDECDROMDevice::doReadCD(uint32_t lba, uint32_t length, uint8_t sector_type,
                              uint8_t main_channel, uint8_t sub_channel, bool data_only)
{
    // We may need to loop if the request spans multiple .bin files
    uint32_t total_length = length;
    uint32_t length_done = 0;
    while (length_done < total_length)
    {
        length = total_length - length_done;

        CUETrackInfo trackinfo = getTrackFromLBA(lba);

        if (!m_image || !selectBinFileForTrack(&trackinfo))
        {
            return atapi_cmd_error(ATAPI_SENSE_NOT_READY, ATAPI_ASC_NO_MEDIUM);
        }

        // Figure out the data offset in the file
        uint64_t offset = trackinfo.file_offset;
        if (lba >= trackinfo.data_start)
        {
            offset += (lba - trackinfo.data_start) * trackinfo.sector_length;
        }
        else if (lba >= trackinfo.data_start - trackinfo.unstored_pregap_length)
        {
            // It doesn't really matter what data we give for the unstored pregap
        }
        else
        {
            // Get data from stored pregap, which is in the file before trackinfo.file_offset.
            uint32_t seek_back = (trackinfo.data_start - lba) * trackinfo.sector_length;
            if (seek_back > offset)
            {
                logmsg("WARNING: Host attempted CD read at sector ", lba, "+", length,
                       " pregap request ", (int)seek_back, " exceeded available ", (int)offset, " for track ", trackinfo.track_number,
                       " (possible .cue file issue)");
                offset = 0;
            }
            else
            {
                offset -= seek_back;
            }
        }

        dbgmsg("---- Read CD: ", (int)length, " sectors starting at ", (int)lba,
            ", track number ", trackinfo.track_number, ", sector size ", (int)trackinfo.sector_length,
            ", main channel ", main_channel, ", sub channel ", sub_channel,
            ", data offset in file ", (int)offset);

        // Ensure read is not out of range of the image
        // If it is, we may be able to get more from the next .bin file.
        uint64_t capacity = m_image->capacity();
        uint32_t sectors_available = (capacity - offset) / trackinfo.sector_length;
        if (length > sectors_available)
        {
            if (sectors_available == 0 || !m_image->is_folder())
            {
                // This is really past the end of the CD
                logmsg("WARNING: Host attempted CD read at sector ", lba, "+", length,
                    ", exceeding image size ", capacity);
                return atapi_cmd_error(ATAPI_SENSE_ILLEGAL_REQ, ATAPI_ASC_LBA_OUT_OF_RANGE);
            }
            else
            {
                // Read as much as we can and continue with next file
                dbgmsg("------ Splitting read request at image file end");
                length = sectors_available;
            }
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
        else if (trackinfo.track_mode == CUETrack_MODE2_2352 && main_channel == 0x10)
        {
            m_cd_read_format.sector_length_file = 2352;
            m_cd_read_format.sector_data_skip = 24;
            m_cd_read_format.sector_data_length = 2048;
            m_cd_read_format.sector_length_out = 2048;
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
        }
        else if (m_image->read(offset, m_cd_read_format.sector_length_file, length, this))
        {
            // Read callback does the work
        }
        else
        {
            dbgmsg("-- CD read failed, starting offset ", (int)offset, " length ", (int)length);
            return atapi_cmd_error(ATAPI_SENSE_MEDIUM_ERROR, 0);
        }

        length_done += length;
        lba += length;
    }

    return atapi_send_wait_finish() && atapi_cmd_ok();
}

ssize_t IDECDROMDevice::read_callback(const uint8_t *data, size_t blocksize, size_t num_blocks)
{
    platform_poll();

    if (num_blocks == 0)
    {
        return 0;
    }

    if (ide_phy_is_command_interrupted())
    {
        dbgmsg("---- IDECDROMDevice::read_callback interrupted by host, sectors_done ", m_cd_read_format.sectors_done);
        return num_blocks;
    }

    if (m_cd_read_format.sector_length_file == m_cd_read_format.sector_length_out)
    {
        // Simple case, send data directly
        return atapi_send_data_async(data, blocksize, num_blocks);
    }

    // Reformat sector data for transmission
    assert(sizeof(m_buffer) >= m_cd_read_format.sector_length_out);
    size_t blocks_done = 0;
    while (blocks_done < num_blocks && atapi_send_data_is_ready(m_cd_read_format.sector_length_out))
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
            const uint8_t *data_start = data + blocksize * blocks_done + m_cd_read_format.sector_data_skip;
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
        ssize_t status = atapi_send_data_async(m_buffer.bytes, m_cd_read_format.sector_length_out, 1);

        if (status < 0)
        {
            dbgmsg("-- IDECDROMDevice atapi_send_data failed, length ", (int)m_cd_read_format.sector_length_out);
            return -1;
        }
        else if (status == 0)
        {
            // Hardware buffer is now full, return from callback
            break;
        }
        else
        {
            // Block written to hardware buffer successfully
            m_cd_read_format.sectors_done += 1;
            blocks_done += 1;
        }
    }

    return blocks_done;
}

bool IDECDROMDevice::loadAndValidateCueSheet(FsFile *dir, const char *cuesheetname)
{
    FsFile cuesheetfile;
    cuesheetfile.open(dir, cuesheetname);
    if (!cuesheetfile.isOpen())
    {
        logmsg("---- No CUE sheet found at ", cuesheetname);
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
    uint64_t prev_capacity = 0;
    while ((trackinfo = m_cueparser.next_track(prev_capacity)) != NULL)
    {
        trackcount++;

        if (trackinfo->track_mode != CUETrack_AUDIO &&
            trackinfo->track_mode != CUETrack_MODE1_2048 &&
            trackinfo->track_mode != CUETrack_MODE1_2352 &&
            trackinfo->track_mode != CUETrack_MODE2_2352)
        {
            logmsg("---- Warning: track ", trackinfo->track_number, " has unsupported mode ", (int)trackinfo->track_mode);
        }

        if (trackinfo->file_mode != CUEFile_BINARY)
        {
            logmsg("---- Unsupported CUE data file mode ", (int)trackinfo->file_mode);
        }

        // Check that the bin file is available
        if (!selectBinFileForTrack(trackinfo))
        {
            return false;
        }
        prev_capacity = m_image->capacity();
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
    uint64_t prev_capacity = 0;
    while ((trackinfo = m_cueparser.next_track(prev_capacity)) != NULL)
    {
        if (!got_track)
        {
            first = *trackinfo;
            got_track = true;
        }

        last = *trackinfo;

        selectBinFileForTrack(trackinfo);
        prev_capacity = m_image->capacity();
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


void IDECDROMDevice::eject_media()
{
#if ENABLE_AUDIO_OUTPUT
    audio_stop();
#endif
    char filename[MAX_FILE_PATH+1];
    m_image->get_filename(filename, sizeof(filename));
    logmsg("Device ejecting media: \"", filename, "\"");
    set_esn_event(esn_event_t::NoChange);
    m_removable.ejected = true;

}

void IDECDROMDevice::button_eject_media()
{
    if (!m_removable.prevent_removable)
        set_esn_event(esn_event_t::MMediaRemoval);
}

void IDECDROMDevice::insert_media(IDEImage *image)
{
#if ENABLE_AUDIO_OUTPUT
    audio_stop();
#endif
    zuluide::images::ImageIterator img_iterator;
    char filename[MAX_FILE_PATH+1];
    if (m_devinfo.removable) 
    {
        if (image != nullptr)
        {
            set_image(image);
            set_esn_event(esn_event_t::MNewMedia);
            m_removable.ejected = false;
            set_not_ready(true);
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
                if (g_ide_imagefile.open_file(img_iterator.Get().GetFilename().c_str(), true))
                {
                    set_image(&g_ide_imagefile);
                    logmsg("-- Device loading media: \"", img_iterator.Get().GetFilename().c_str(), "\"");
                    set_esn_event(esn_event_t::MNewMedia);
                    m_removable.ejected = false;
                    set_not_ready(true);
                }
            }
            img_iterator.Cleanup();
        }
    }
}

void IDECDROMDevice::set_esn_event(esn_event_t event)
{
    if (event == esn_event_t::MEjectRequest || event == esn_event_t::MNewMedia || event == esn_event_t::MMediaRemoval)
    {
        m_esn.event = event;
        m_esn.request = esn_class_request_t::Media;
        m_esn.current_event = esn_event_t::NoChange;
    }
    else
    {
        m_esn.event = esn_event_t::NoChange;
        m_esn.request = esn_class_request_t::OperationChange;
        m_esn.current_event = esn_event_t::NoChange;
    }
}

void IDECDROMDevice::esn_next_event()
{
    switch (m_esn.event)
    {
        case esn_event_t::MNewMedia :
        case esn_event_t::MEjectRequest :
            if (m_esn.current_event == esn_event_t::NoChange)
            {
                m_esn.current_event = m_esn.event;
                return;
            }
            break;
        case esn_event_t::MMediaRemoval :
            if (m_esn.current_event == esn_event_t::NoChange)
            {
                m_esn.current_event = esn_event_t::MEjectRequest;
                return;
            }
            if (m_esn.current_event == esn_event_t::MEjectRequest)
            {
                m_esn.current_event =  m_esn.event;
                return;
            }
            break;
        default:
            break;
    }
    set_esn_event(esn_event_t::NoChange);
}

uint32_t IDECDROMDevice::getLeadOutLBA(const CUETrackInfo* lasttrack)
{
    if (lasttrack != nullptr && lasttrack->track_number != 0 && m_image != nullptr)
    {
        selectBinFileForTrack(lasttrack);
        uint32_t lastTrackBlocks = (m_image->capacity() - lasttrack->file_offset) / lasttrack->sector_length;
        return lasttrack->data_start + lastTrackBlocks;
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
    uint64_t prev_capacity = 0;
    m_cueparser.restart();
    while ((tmptrack = m_cueparser.next_track(prev_capacity)) != NULL)
    {
        if (tmptrack->track_start <= lba)
        {
            result = *tmptrack;
        }
        else
        {
            break;
        }

        selectBinFileForTrack(tmptrack);
        prev_capacity = m_image->capacity();
    }

    return result;
}

// Check if we need to switch the data .bin file when track changes.
bool IDECDROMDevice::selectBinFileForTrack(const CUETrackInfo *track)
{
    if (track->filename[0] == '\0' || !m_image->is_folder())
    {
        // Using a single image, no need to switch anything.
        return true;
    }
    else if (m_selected_file_index == track->file_index)
    {
        // Haven't switched files
        return true;
    }

    m_selected_file_index = track->file_index;

    char filename[MAX_FILE_PATH + 1];
    if (m_image->get_filename(filename, sizeof(filename)) &&
        strncasecmp(track->filename, filename, sizeof(filename)) == 0)
    {
        // We already have the correct binfile open.
        return true;
    }

    bool open_ok = m_image->select_image(track->filename);

    if (!open_ok)
    {
        logmsg("CUE sheet specified track file '", track->filename, "' not found");
    }

    return open_ok;
}

size_t IDECDROMDevice::atapi_get_configuration(uint8_t return_type, uint16_t feature, uint8_t *buffer, size_t max_bytes)
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

#ifdef ENABLE_AUDIO_OUTPUT
    // CD audio feature (0x103, 259)
    if (feature == ATAPI_FEATURE_CDAUDIO 
        && (return_type == ATAPI_RT_SINGLE 
            || return_type == ATAPI_RT_ALL
            || (return_type == ATAPI_RT_ALL_CURRENT && !m_removable.ejected)))
    {
        write_be16(&buffer[0], feature);
        buffer[2] = (m_removable.ejected) ? 0x04 : 0x05;
        buffer[3] = 4;     // length
        buffer[4] = 0x03; // scan=0,scm=1,sv=1
        buffer[5] = 0; // resevered
        write_be16(&buffer[6], 256); // numer of audio levels
        return 8;
    }
#endif

    return IDEATAPIDevice::atapi_get_configuration(return_type, feature, buffer, max_bytes);
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
#ifdef ENABLE_AUDIO_OUTPUT
    if (page_idx == ATAPI_MODESENSE_CD_AUDIO_CONTROL)
    {
        uint16_t vol = audio_get_volume();
        uint8_t r_vol = vol >> 8;
        uint8_t l_vol = vol & 0xFF;
        uint16_t ch = audio_get_channel() & AUDIO_CHANNEL_ENABLE_MASK;
        uint8_t l_ch = ch & 0xFF;
        uint8_t r_ch = ch >> 8;
        dbgmsg("Volume returns: lc ", l_ch, " lv ", l_vol, " rc ", r_ch, " rv ", r_vol);
        buffer[0] = ATAPI_MODESENSE_CD_AUDIO_CONTROL;
        buffer[1] = 14; // page length
        buffer[2] = 0x04; // 'Immed' bti set, 'SOTC' bit not set
        buffer[3] = 0x00; // reserved
        buffer[4] = 0x00; // reserved
        buffer[5] = 0x00; // reserved
        buffer[6] = 0x00; // obsolete
        buffer[7] = 0x00; // obsolete
        buffer[8] = l_ch; // output port 0
        buffer[9] = l_vol; // port 0 - left volume
        buffer[10] = r_ch; // output port 1
        buffer[11] = r_vol; // port 1 - right volume
        buffer[12] = 0x00; // output port 3 inactive
        buffer[13] = 0x00; // output port 3 inactive
        buffer[14] = 0x00; // output port 4 inactive
        buffer[15] = 0x00; // output port 4 inactive
    return 16;

    }
#endif

    if (page_idx == ATAPI_MODESENSE_CD_CAPABILITIES)
    {
        buffer[0] = ATAPI_MODESENSE_CD_CAPABILITIES;
        buffer[1] = 14; // page length
        buffer[2] = 0x00; // CD-R/RW reading not supported
        buffer[3] = 0x00; // CD-R/RW writing not supported
#ifdef ENABLE_AUDIO_OUTPUT
        buffer[4] = 0x01; // byte 4: audio play supported
#else
        buffer[4] = 0x00; // byte 4: no features supported
#endif
        buffer[5] = 0x03; // byte 5: CD-DA ok with accurate streaming, no other features
        buffer[6] = 0x28; // byte 6: tray loader, ejection ok, but prevent/allow not supported
#ifdef ENABLE_AUDIO_OUTPUT
        buffer[7] = 0x03; // byte 7: separate channel mute and volumes
#else
        buffer[7] = 0x00; // byte 7: no features supported
#endif
        write_be16(&buffer[8],6292);  // max read speed, state (40, ~6292KB/s)
#ifdef ENABLE_AUDIO_OUTPUT
        write_be16(&buffer[10], 256); // 256 volume levels supported
#else
        write_be16(&buffer[10], 0); // no volume levels supported
#endif
        write_be16(&buffer[12], 64); // read buffer (64KB)
        write_be16(&buffer[14],6292);  // current max read speed, state (40, ~6292KB/s)
    }
    return IDEATAPIDevice::atapi_get_mode_page(page_ctrl, page_idx, buffer, max_bytes);
}

void IDECDROMDevice::atapi_set_mode_page(uint8_t page_ctrl, uint8_t page_idx, const uint8_t *buffer, size_t length)
{
#ifdef ENABLE_AUDIO_OUTPUT
    if (page_idx == ATAPI_MODESENSE_CD_AUDIO_CONTROL)
    {

        uint8_t l_ch = buffer[8];  // output port 0
        uint8_t l_vol = buffer[9]; // port 0 - left volume
        uint8_t r_ch = buffer[10];  // output port 1
        uint8_t r_vol = buffer[11]; // port 1 - right volume;

        audio_set_channel((r_ch << 8) | l_ch);
        audio_set_volume(l_vol, r_vol);
    }
#endif
    IDEATAPIDevice::atapi_set_mode_page(page_ctrl, page_idx, buffer, length);
}

/**************************************/
/* CD-ROM audio playback              */
/**************************************/


bool IDECDROMDevice::doPlayAudio(uint32_t lba, uint32_t length)
{
#ifdef ENABLE_AUDIO_OUTPUT
    dbgmsg("------ CD-ROM Play Audio request at ", (int) lba, " for ",  (int)length, " sectors");

    // Per Annex C terminate playback immediately if already in progress on
    // the current target. Non-current targets may also get their audio
    // interrupted later due to hardware limitations
    audio_stop();


    // if actual playback is requested perform steps to verify prior to playback
    if (m_devinfo.medium_type == ATAPI_MEDIUM_CDDA || m_devinfo.medium_type == ATAPI_MEDIUM_CDMIXED)
    {

        if (lba == 0xFFFFFFFF)
        {
            // request to start playback from 'current position'
            lba = audio_get_lba_position();
        }

        CUETrackInfo trackinfo = getTrackFromLBA(lba);
        m_cd_read_format.trackinfo = trackinfo;

        uint64_t offset = m_cd_read_format.trackinfo.file_offset
                + trackinfo.sector_length * (lba - trackinfo.track_start);
        dbgmsg("------ Play audio CD: ", (int)length, " sectors starting at ", (int)lba,
           ", track number ", trackinfo.track_number, ", data offset in file ", (int)offset);

        if (trackinfo.track_mode != CUETrack_AUDIO)
        {
            dbgmsg("---- Host tried audio playback on track type ", (int)trackinfo.track_mode);
            return atapi_cmd_error(ATAPI_SENSE_ILLEGAL_REQ, ATAPI_ASC_ILLEGAL_MODE_FOR_TRACK);
        }

        // if transfer length is zero no audio playback happens.
        // don't treat as an error per SCSI-2; audio_play returns true

        // playback request appears to be sane, so perform it
        // see earlier note for context on the block length below
        if (!audio_play(lba, length, false))
        {
            // Underlying data/media error? Fake a disk scratch, which should
            // be a condition most CD-DA players are expecting
            return atapi_cmd_error(ATAPI_SENSE_MEDIUM_ERROR, ATAPI_CIRC_UNRECOVERED_ERROR);
        }
        return atapi_cmd_ok();
    }
    else
    {
        // virtual drive supports audio, just not with this disk image
        dbgmsg("---- Request to play audio on non-audio image");
        return atapi_cmd_error(ATAPI_SENSE_ILLEGAL_REQ, ATAPI_ASC_ILLEGAL_MODE_FOR_TRACK);
    }
#else
    dbgmsg("---- Target does not support audio playback");
    // per SCSI-2, targets not supporting audio respond to zero-length
    // PLAY AUDIO commands with ILLEGAL REQUEST; this seems to be a check
    // performed by at least some audio playback software
    return atapi_cmd_error(ATAPI_SENSE_ILLEGAL_REQ, ATAPI_ASC_NO_ASC);
#endif
}




