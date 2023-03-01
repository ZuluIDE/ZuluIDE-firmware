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
X(ATAPI_CMD_INQUIRY                       , 0x12) \
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
X(ATAPI_CMD_READ_TOC                      , 0x43) \
X(ATAPI_CMD_GET_CONFIGURATION             , 0x46) \
X(ATAPI_CMD_GET_EVENT_STATUS_NOTIFICATION , 0x4A) \
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

#define ATAPI_ASC_INVALID_CMD       0x2000

// ATAPI INQUIRY response format
#define ATAPI_INQUIRY_OFFSET_TYPE       0
#define ATAPI_INQUIRY_REMOVABLE_MEDIA   1
#define ATAPI_INQUIRY_VERSION           2
#define ATAPI_INQUIRY_ATAPI_VERSION     3
#define ATAPI_INQUIRY_EXTRA_LENGTH      4
#define ATAPI_INQUIRY_VENDOR            8
#define ATAPI_INQUIRY_PRODUCT           16
#define ATAPI_INQUIRY_REVISION          32
