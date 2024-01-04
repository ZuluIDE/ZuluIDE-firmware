// Compile-time configuration parameters.
// Other settings can be set by ini file at runtime.

#pragma once

#include <ZuluIDE_platform.h>

// Use variables for version number
#define FW_VER_NUM      "2024.01.04"
#define FW_VER_SUFFIX   "dev"
#define ZULU_FW_VERSION FW_VER_NUM "-" FW_VER_SUFFIX

// Configuration and log file paths
#define CONFIGFILE  "zuluide.ini"
#define LOGFILE     "zululog.txt"
#define CRASHFILE   "zuluerr.txt"
#define LICENSEFILE "zuluide.lic"

// Maximum path length for files on SD card
#define MAX_FILE_PATH 64

// Transfer buffer size in bytes, must be a power of 2
#ifndef IDE_BUFFER_SIZE
#define IDE_BUFFER_SIZE 65536
#endif

// Log buffer size in bytes, must be a power of 2
#ifndef LOGBUFSIZE
#define LOGBUFSIZE 16384
#endif
#define LOG_SAVE_INTERVAL_MS 1000

// Watchdog timeout
// Watchdog will first issue a bus reset and if that does not help, crashdump.
#define WATCHDOG_BUS_RESET_TIMEOUT 15000
#define WATCHDOG_CRASH_TIMEOUT 30000
