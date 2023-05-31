// Standard values for IDE protocol
// Refer to https://pdos.csail.mit.edu/6.828/2005/readings/hardware/ATA-d1410r3a.pdf
// (with TOC https://jpa.kapsi.fi/stuff/other/ATA-d1410r3a_toc.pdf )
#pragma once
#include <stdint.h>

// Status register bits
#define IDE_STATUS_BSY      0x80
#define IDE_STATUS_DEVRDY   0x40
#define IDE_STATUS_DEVFAULT 0x20
#define IDE_STATUS_SERVICE  0x10
#define IDE_STATUS_DATAREQ  0x08
#define IDE_STATUS_ERR      0x01

// Error register bits
// Note that these vary by command type
#define IDE_ERROR_WRITEPROTECT    0x40
#define IDE_ERROR_MEDIACHANGE     0x20
#define IDE_ERROR_MEDIACHANGEREQ  0x08
#define IDE_ERROR_ABORT           0x04
#define IDE_ERROR_NOMEDIA         0x01

// Device control register bits
#define IDE_DEVCTRL_nIEN    0x02
#define IDE_DEVCTRL_SRST    0x04
#define IDE_DEVCTRL_HOB     0x80

// IDE command set defined as X-macro
// These will be available as enum values named IDE_CMD_xxx
#define IDE_COMMAND_LIST(X) \
X(IDE_CMD_NOP                                       , 0x00) \
X(IDE_CMD_CFA_REQUEST_EXTENDED_ERROR                , 0x03) \
X(IDE_CMD_DEVICE_RESET                              , 0x08) \
X(IDE_CMD_READ_SECTORS                              , 0x20) \
X(IDE_CMD_READ_SECTORS_EXT                          , 0x24) \
X(IDE_CMD_READ_DMA_EXT                              , 0x25) \
X(IDE_CMD_READ_DMA_QUEUED_EXT                       , 0x26) \
X(IDE_CMD_READ_NATIVE_MAX_ADDRESS_EXT               , 0x27) \
X(IDE_CMD_READ_MULTIPLE_EXT                         , 0x29) \
X(IDE_CMD_READ_LOG_EXT                              , 0x2F) \
X(IDE_CMD_WRITE_SECTORS                             , 0x30) \
X(IDE_CMD_WRITE_SECTORS_EXT                         , 0x34) \
X(IDE_CMD_WRITE_DMA_EXT                             , 0x35) \
X(IDE_CMD_WRITE_DMA_QUEUED_EXT                      , 0x36) \
X(IDE_CMD_SET_MAX_ADDRESS_EXT                       , 0x37) \
X(IDE_CMD_CFA_WRITE_SECTORS_WOUT_ERASE              , 0x38) \
X(IDE_CMD_WRITE_MULTIPLE_EXT                        , 0x39) \
X(IDE_CMD_WRITE_LOG_EXT                             , 0x3F) \
X(IDE_CMD_READ_VERIFY_SECTORS                       , 0x40) \
X(IDE_CMD_READ_VERIFY_SECTORS_EXT                   , 0x42) \
X(IDE_CMD_SEEK                                      , 0x70) \
X(IDE_CMD_CFA_TRANSLATE_SECTOR                      , 0x87) \
X(IDE_CMD_EXECUTE_DEVICE_DIAGNOSTIC                 , 0x90) \
X(IDE_CMD_INIT_DEV_PARAMS                           , 0x91) \
X(IDE_CMD_DOWNLOAD_MICROCODE                        , 0x92) \
X(IDE_CMD_PACKET                                    , 0xA0) \
X(IDE_CMD_IDENTIFY_PACKET_DEVICE                    , 0xA1) \
X(IDE_CMD_SERVICE                                   , 0xA2) \
X(IDE_CMD_SMART                                     , 0xB0) \
X(IDE_CMD_DEVICE_CONFIGURATION                      , 0xB1) \
X(IDE_CMD_CFA_ERASE_SECTORS                         , 0xC0) \
X(IDE_CMD_READ_MULTIPLE                             , 0xC4) \
X(IDE_CMD_WRITE_MULTIPLE                            , 0xC5) \
X(IDE_CMD_SET_MULTIPLE_MODE                         , 0xC6) \
X(IDE_CMD_READ_DMA_QUEUED                           , 0xC7) \
X(IDE_CMD_READ_DMA                                  , 0xC8) \
X(IDE_CMD_WRITE_DMA                                 , 0xCA) \
X(IDE_CMD_WRITE_DMA_QUEUED                          , 0xCC) \
X(IDE_CMD_CFA_WRITE_MULTIPLE_WOUT_ERASE             , 0xCD) \
X(IDE_CMD_CHECK_MEDIA_CARD_TYPE                     , 0xD1) \
X(IDE_CMD_GET_MEDIA_STATUS                          , 0xDA) \
X(IDE_CMD_MEDIA_LOCK                                , 0xDE) \
X(IDE_CMD_MEDIA_UNLOCK                              , 0xDF) \
X(IDE_CMD_STANDBY_IMMEDIATE                         , 0xE0) \
X(IDE_CMD_IDLE_IMMEDIATE                            , 0xE1) \
X(IDE_CMD_STANDBY                                   , 0xE2) \
X(IDE_CMD_IDLE                                      , 0xE3) \
X(IDE_CMD_READ_BUFFER                               , 0xE4) \
X(IDE_CMD_CHECK_POWER_MODE                          , 0xE5) \
X(IDE_CMD_SLEEP                                     , 0xE6) \
X(IDE_CMD_FLUSH_CACHE                               , 0xE7) \
X(IDE_CMD_WRITE_BUFFER                              , 0xE8) \
X(IDE_CMD_FLUSH_CACHE_EXT                           , 0xEA) \
X(IDE_CMD_IDENTIFY_DEVICE                           , 0xEC) \
X(IDE_CMD_MEDIA_EJECT                               , 0xED) \
X(IDE_CMD_SET_FEATURES                              , 0xEF) \
X(IDE_CMD_SECURITY_SET_PASSWORD                     , 0xF1) \
X(IDE_CMD_SECURITY_UNLOCK                           , 0xF2) \
X(IDE_CMD_SECURITY_ERASE_PREPARE                    , 0xF3) \
X(IDE_CMD_SECURITY_ERASE_UNIT                       , 0xF4) \
X(IDE_CMD_SECURITY_FREEZE_LOCK                      , 0xF5) \
X(IDE_CMD_SECURITY_DISABLE_PASSWORD                 , 0xF6) \
X(IDE_CMD_READ_NATIVE_MAX_ADDRESS                   , 0xF8) \
X(IDE_CMD_SET_MAX_ADDRESS                           , 0xF9)

enum ide_cmd_t {
#define IDE_ENUM_ENTRY(name, code) name = code,
IDE_COMMAND_LIST(IDE_ENUM_ENTRY)
#undef IDE_ENUM_ENTRY 
};

// IDE_CMD_IDENTIFY_DEVICE response format
#define IDE_IDENTIFY_OFFSET_GENERAL_CONFIGURATION     0
#define IDE_IDENTIFY_OFFSET_NUM_CYLINDERS             1
#define IDE_IDENTIFY_OFFSET_SPECIFIC_CONFIGURATION    2
#define IDE_IDENTIFY_OFFSET_NUM_HEADS                 3
#define IDE_IDENTIFY_OFFSET_BYTES_PER_TRACK           4
#define IDE_IDENTIFY_OFFSET_BYTES_PER_SECTOR          5
#define IDE_IDENTIFY_OFFSET_SECTORS_PER_TRACK         6
#define IDE_IDENTIFY_OFFSET_SERIAL_NUMBER            10
#define IDE_IDENTIFY_OFFSET_FIRMWARE_REV             23
#define IDE_IDENTIFY_OFFSET_MODEL_NUMBER             27
#define IDE_IDENTIFY_OFFSET_MAX_SECTORS              47
#define IDE_IDENTIFY_OFFSET_CAPABILITIES_1           49
#define IDE_IDENTIFY_OFFSET_CAPABILITIES_2           50
#define IDE_IDENTIFY_OFFSET_MODE_INFO_VALID          53
#define IDE_IDENTIFY_OFFSET_MULTI_SECTOR_VALID       59
#define IDE_IDENTIFY_OFFSET_TOTAL_SECTORS            60
#define IDE_IDENTIFY_OFFSET_MODEINFO_MULTIWORD       63
#define IDE_IDENTIFY_OFFSET_MODEINFO_PIO             64
#define IDE_IDENTIFY_OFFSET_MULTIWORD_CYCLETIME_MIN  65
#define IDE_IDENTIFY_OFFSET_MULTIWORD_CYCLETIME_REC  66
#define IDE_IDENTIFY_OFFSET_PIO_CYCLETIME_MIN        67
#define IDE_IDENTIFY_OFFSET_PIO_CYCLETIME_IORDY      68
#define IDE_IDENTIFY_OFFSET_QUEUE_DEPTH              75
#define IDE_IDENTIFY_OFFSET_STANDARD_VERSION_MAJOR   80
#define IDE_IDENTIFY_OFFSET_STANDARD_VERSION_MINOR   81
#define IDE_IDENTIFY_OFFSET_COMMAND_SET_SUPPORT_1    82
#define IDE_IDENTIFY_OFFSET_COMMAND_SET_SUPPORT_2    83
#define IDE_IDENTIFY_OFFSET_COMMAND_SET_SUPPORT_3    84
#define IDE_IDENTIFY_OFFSET_COMMAND_SET_ENABLED_1    85
#define IDE_IDENTIFY_OFFSET_COMMAND_SET_ENABLED_2    86
#define IDE_IDENTIFY_OFFSET_COMMAND_SET_INFO         87
#define IDE_IDENTIFY_OFFSET_MODEINFO_ULTRADMA        88
#define IDE_IDENTIFY_OFFSET_SECURITY_ERASE_TIME      89
#define IDE_IDENTIFY_OFFSET_ENH_SECURITY_ERASE_TIME  90
#define IDE_IDENTIFY_OFFSET_POWER_MANAGEMENT         91
#define IDE_IDENTIFY_OFFSET_MASTER_PASSWORD_REV      92
#define IDE_IDENTIFY_OFFSET_HARDWARE_RESET_RESULT    93
#define IDE_IDENTIFY_OFFSET_ACOUSTIC_MANAGEMENT      94
#define IDE_IDENTIFY_OFFSET_MAX_LBA                 100
#define IDE_IDENTIFY_OFFSET_REMOVABLE_MEDIA_SUPPORT 127
#define IDE_IDENTIFY_OFFSET_SECURITY_STATUS         128
#define IDE_IDENTIFY_OFFSET_CFA_POWER_MODE_1        160
#define IDE_IDENTIFY_OFFSET_MEDIA_SERIAL_NUMBER     176
#define IDE_IDENTIFY_OFFSET_INTEGRITY_WORD          255

// IDE_CMD_SET_FEATURES feature register values
#define IDE_SET_FEATURE_ENABLE_8BIT                 0x01
#define IDE_SET_FEATURE_ENABLE_WRITE_CACHE          0x02
#define IDE_SET_FEATURE_TRANSFER_MODE               0x03
#define IDE_SET_FEATURE_DISABLE_RETRY               0x33
#define IDE_SET_FEATURE_VENDOR_ECC                  0x44
#define IDE_SET_FEATURE_CACHE_SEGMENTS              0x54
#define IDE_SET_FEATURE_ENABLE_RELEASE_IRQ          0x5D
#define IDE_SET_FEATURE_ENABLE_SERVICE_IRQ          0x5E
#define IDE_SET_FEATURE_DISABLE_READ_AHEAD          0x55
#define IDE_SET_FEATURE_DISABLE_REVERT_TO_POWERON   0x66
#define IDE_SET_FEATURE_DISABLE_ECC                 0x77
#define IDE_SET_FEATURE_DISABLE_8BIT                0x81
#define IDE_SET_FEATURE_DISABLE_WRITE_CACHE         0x82
#define IDE_SET_FEATURE_ENABLE_ECC                  0x88
#define IDE_SET_FEATURE_ENABLE_RETRY                0x99
#define IDE_SET_FEATURE_ENABLE_READ_AHEAD           0xAA
#define IDE_SET_FEATURE_MAXIMUM_PREFETCH            0xAB
#define IDE_SET_FEATURE_READ_LONG_ECC               0xBB
#define IDE_SET_FEATURE_ENABLE_REVERT_TO_POWERON    0xCC
#define IDE_SET_FEATURE_DISABLE_RELEASE_IRQ         0xDD
#define IDE_SET_FEATURE_DISABLE_SERVICE_IRQ         0xDE
