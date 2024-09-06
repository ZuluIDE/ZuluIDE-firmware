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

// Standard values for ATAPI protocol
// Refer to :
// https://www.staff.uni-mainz.de/tacke/scsi/SCSI2-contents.html
// http://www.13thmonkey.org/documentation/SCSI/mmc4r05a.pdf
// http://suif.stanford.edu/~csapuntz/specs/INF-8020.PDF
// (with TOC https://jpa.kapsi.fi/stuff/other/INF-8020_toc.PDF )

#pragma once
#include <stdint.h>

// ATAPI command set defined as X-macro
// These will be available as enum values named ATAPI_CMD_xxx
#define ATAPI_COMMAND_LIST(X) \
X(ATAPI_CMD_TEST_UNIT_READY               , 0x00) \
X(ATAPI_CMD_REQUEST_SENSE                 , 0x03) \
X(ATAPI_CMD_FORMAT_UNIT                   , 0x04) \
X(ATAPI_CMD_VENDOR_0x06                   , 0x06) \
X(ATAPI_CMD_READ6                         , 0x08) \
X(ATAPI_CMD_WRITE6                        , 0x0A) \
X(ATAPI_CMD_SEEK_6                        , 0x0B) \
X(ATAPI_CMD_VENDOR_0x0D                   , 0x0D) \
X(ATAPI_CMD_INQUIRY                       , 0x12) \
X(ATAPI_CMD_MODE_SELECT6                  , 0x15) \
X(ATAPI_CMD_MODE_SENSE6                   , 0x1A) \
X(ATAPI_CMD_START_STOP_UNIT               , 0x1B) \
X(ATAPI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL  , 0x1E) \
X(ATAPI_CMD_READ_FORMAT_CAPACITIES        , 0x23) \
X(ATAPI_CMD_READ_CAPACITY                 , 0x25) \
X(ATAPI_CMD_READ10                        , 0x28) \
X(ATAPI_CMD_WRITE10                       , 0x2A) \
X(ATAPI_CMD_SEEK10                        , 0x2B) \
X(ATAPI_CMD_WRITE_AND_VERIFY10            , 0x2E) \
X(ATAPI_CMD_VERIFY10                      , 0x2F) \
X(ATAPI_CMD_SYNCHRONIZE_CACHE             , 0x35) \
X(ATAPI_CMD_WRITE_BUFFER                  , 0x3B) \
X(ATAPI_CMD_READ_BUFFER                   , 0x3C) \
X(ATAPI_CMD_READ_SUB_CHANNEL              , 0x42) \
X(ATAPI_CMD_READ_TOC                      , 0x43) \
X(ATAPI_CMD_READ_HEADER                   , 0x44) \
X(ATAPI_CMD_PLAY_AUDIO_10                 , 0x45) \
X(ATAPI_CMD_GET_CONFIGURATION             , 0x46) \
x(ATAPI_CMD_PLAY_AUDIO_MSF                , 0x47) \
X(ATAPI_CMD_GET_EVENT_STATUS_NOTIFICATION , 0x4A) \
x(ATAPI_CMD_PAUSE_RESUME_AUDIO            , 0x4B) \
X(ATAPI_CMD_STOP_PLAY_SCAN_AUDIO            0x4E) \
X(ATAPI_CMD_READ_DISC_INFORMATION         , 0x51) \
X(ATAPI_CMD_READ_TRACK_INFORMATION        , 0x52) \
X(ATAPI_CMD_RESERVE_TRACK                 , 0x53) \
X(ATAPI_CMD_SEND_OPC_INFORMATION          , 0x54) \
X(ATAPI_CMD_MODE_SELECT10                 , 0x55) \
X(ATAPI_CMD_REPAIR_TRACK                  , 0x58) \
X(ATAPI_CMD_MODE_SENSE10                  , 0x5A) \
X(ATAPI_CMD_CLOSE_TRACK_SESSION           , 0x5B) \
X(ATAPI_CMD_READ_BUFFER_CAPACITY          , 0x5C) \
X(ATAPI_CMD_SEND_CUE_SHEET                , 0x5D) \
X(ATAPI_CMD_REPORT_LUNS                   , 0xA0) \
X(ATAPI_CMD_BLANK                         , 0xA1) \
X(ATAPI_CMD_SECURITY_PROTOCOL_IN          , 0xA2) \
X(ATAPI_CMD_SEND_KEY                      , 0xA3) \
X(ATAPI_CMD_REPORT_KEY                    , 0xA4) \
X(ATAPI_CMD_PLAY_AUDIO_12                 , 0xA5) \
X(ATAPI_CMD_LOAD_UNLOAD_MEDIUM            , 0xA6) \
X(ATAPI_CMD_SET_READ_AHEAD                , 0xA7) \
X(ATAPI_CMD_READ12                        , 0xA8) \
X(ATAPI_CMD_WRITE12                       , 0xAA) \
X(ATAPI_CMD_READ_MEDIA_SERIAL_NUMBER      , 0xAB) \
X(ATAPI_CMD_GET_PERFORMANCE               , 0xAC) \
X(ATAPI_CMD_READ_DISC_STRUCTURE           , 0xAD) \
X(ATAPI_CMD_SECURITY_PROTOCOL_OUT         , 0xB5) \
X(ATAPI_CMD_SET_STREAMING                 , 0xB6) \
X(ATAPI_CMD_READ_CD_MSF                   , 0xB9) \
X(ATAPI_CMD_SET_CD_SPEED                  , 0xBB) \
X(ATAPI_CMD_MECHANISM_STATUS              , 0xBD) \
X(ATAPI_CMD_READ_CD                       , 0xBE) \
X(ATAPI_CMD_SEND_DISC_STRUCTURE           , 0xBF)

enum atapi_cmd_t {
#define ATAPI_ENUM_ENTRY(name, code) name = code,
ATAPI_COMMAND_LIST(ATAPI_ENUM_ENTRY)
#undef ATAPI_ENUM_ENTRY
};

// ATAPI status register
#define ATAPI_STATUS_BSY            0x80
#define ATAPI_STATUS_DRDY           0x40
#define ATAPI_STATUS_DMARDY         0x20
#define ATAPI_STATUS_SERVICE        0x10
#define ATAPI_STATUS_DATAREQ        0x08
#define ATAPI_STATUS_CORRECTION     0x04
#define ATAPI_STATUS_CHECK          0x00

// Sector count register bits, used for interrupt reason in ATAPI
#define ATAPI_SCOUNT_IS_CMD         0x01
#define ATAPI_SCOUNT_TO_HOST        0x02
#define ATAPI_SCOUNT_RELEASE        0x04

// ATAPI error reporting
#define ATAPI_SENSE_NO_SENSE        0x00
#define ATAPI_SENSE_RECOVERED       0x01
#define ATAPI_SENSE_NOT_READY       0x02
#define ATAPI_SENSE_MEDIUM_ERROR    0x03
#define ATAPI_SENSE_HARDWARE_ERROR  0x04
#define ATAPI_SENSE_ILLEGAL_REQ     0x05
#define ATAPI_SENSE_UNIT_ATTENTION  0x06
#define ATAPI_SENSE_DATA_PROTECT    0x07
#define ATAPI_SENSE_ABORTED_CMD     0x0B
#define ATAPI_SENSE_MISCOMPARE      0x0E


#define ATAPI_ASC_NO_ASC                    0x0000
#define ATAPI_ASC_CRC_ERROR                 0x0803
#define ATAPI_CIRC_UNRECOVERED_ERROR        0x1106
#define ATAPI_ASC_PARAMETER_LENGTH_ERROR    0x1A00
#define ATAPI_ASC_INVALID_CMD               0x2000
#define ATAPI_ASC_LBA_OUT_OF_RANGE          0x2100
#define ATAPI_ASC_INVALID_FIELD             0x2400
#define ATAPI_ASC_WRITE_PROTECTED           0x2700
#define ATAPI_ASC_MEDIUM_CHANGE             0x2800
#define ATAPI_ASC_RESET_OCCURRED            0x2900
#define ATAPI_ASC_COMMAND_SEQUENCE_ERROR    0x2C00
#define ATAPI_ASC_NO_MEDIUM                 0x3A00
#define ATAPI_ASC_NO_MEDIUM_TRAY_OPEN       0x3A02
#define ATAPI_ASC_UNIT_BECOMING_READY       0x0401
#define ATAPI_ASC_MEDIUM_REMOVAL_PREVENTED  0x5302
#define ATAPI_ASC_ILLEGAL_MODE_FOR_TRACK    0x6400

// ATAPI INQUIRY response format
#define ATAPI_INQUIRY_OFFSET_TYPE       0
#define ATAPI_INQUIRY_REMOVABLE_MEDIA   1
#define ATAPI_INQUIRY_VERSION           2
#define ATAPI_INQUIRY_ATAPI_VERSION     3
#define ATAPI_INQUIRY_EXTRA_LENGTH      4
#define ATAPI_INQUIRY_VENDOR            8
#define ATAPI_INQUIRY_PRODUCT           16
#define ATAPI_INQUIRY_REVISION          32

// ATAPI device types
#define ATAPI_DEVTYPE_DIRECT_ACCESS     0
#define ATAPI_DEVTYPE_CDROM             5

// ATAPI medium types
#define ATAPI_MEDIUM_UNKNOWN            0x00
#define ATAPI_MEDIUM_CDROM              0x01
#define ATAPI_MEDIUM_CDDA               0x02
#define ATAPI_MEDIUM_CDMIXED            0x03
#define ATAPI_MEDIUM_NONE               0x70

// GET_CONFIGURATION profiles
#define ATAPI_PROFILE_RESERVED          0x0000
#define ATAPI_PROFILE_FIXEDDISK         0x0001
#define ATAPI_PROFILE_REMOVABLE         0x0002
#define ATAPI_PROFILE_CDROM             0x0008

// GET_CONFIGURATION return types (rt)
#define ATAPI_RT_ALL            0x0
#define ATAPI_RT_ALL_CURRENT    0x1
#define ATAPI_RT_SINGLE         0x2

// GET_CONFIGURATION features
#define ATAPI_FEATURE_PROFILES          0x0000
#define ATAPI_FEATURE_CORE              0x0001
#define ATAPI_FEATURE_REMOVABLE         0x0003
#define ATAPI_FEATURE_CDREAD            0x001E
#define ATAPI_FEATURE_CDAUDIO           0x0103
#define ATAPI_FEATURE_MAX               0x0032



// MODE SENSE pages
#define ATAPI_MODESENSE_ERRORRECOVERY   0x01
#define ATAPI_MODESENSE_GEOMETRY        0x04
#define ATAPI_MODESENSE_FLEXDISK        0x05
#define ATAPI_MODESENSE_CACHING         0x08
#define ATAPI_MODESENSE_CDROM           0x0D

// ATAPI GET EVENT STATUS events
#define ATAPI_MEDIA_EVENT_NOCHG         0x00
#define ATAPI_MEDIA_EVENT_EJECTREQ      0x01
#define ATAPI_MEDIA_EVENT_NEW           0x02
#define ATAPI_MEDIA_EVENT_REMOVED       0x03
#define ATAPI_MEDIA_EVENT_CHANGERREQ    0x04
#define ATAPI_MEDIA_EVENT_FORMATDONE    0x05
#define ATAPI_MEDIA_EVENT_FORMATRESTART 0x06

// ATAPI START_STOP
#define ATAPI_START_STOP_EJT_OFFSET     0x04
#define ATAPI_START_STOP_START          0x01
#define ATAPI_START_STOP_LOEJ           0x02
#define ATAPI_START_STOP_PWR_CON_MASK   (0x07 << 4)


