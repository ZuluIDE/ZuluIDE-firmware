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

// Compile-time configuration parameters.
// Other settings can be set by ini file at runtime.

#pragma once

#include <ZuluIDE_platform.h>

// Use variables for version number
#define FW_VER_NUM      "2024.05.02"
#define FW_VER_SUFFIX   "devel"
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
