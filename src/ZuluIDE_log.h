/** 
 * ZuluIDE™ - Copyright (c) 2022 Rabbit Hole Computing™
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

// Helpers for log messages.

#pragma once

#include <stdint.h>
#include <stddef.h>

// Get total number of bytes that have been written to log
uint32_t log_get_buffer_len();

// Get log as a string.
// If startpos is given, continues log reading from previous position and updates the position.
// If available is given, number of bytes available is written there.
const char *log_get_buffer(uint32_t *startpos, uint32_t *available = nullptr);

// Whether to enable debug messages
extern bool g_log_debug;

// Firmware version string
extern const char *g_log_firmwareversion;

// Log string
void log_raw(const char *str);

// Log byte as hex
void log_raw(uint8_t value);

// Log integer as hex
void log_raw(uint16_t value);

// Log integer as hex
void log_raw(uint32_t value);

// Log integer as hex
void log_raw(uint64_t value);

// Log integer as decimal
void log_raw(int value);

// Log array of bytes
struct bytearray {
    bytearray(const uint8_t *data, size_t len): data(data), len(len) {}
    const uint8_t *data;
    size_t len;
};
void log_raw(bytearray array);

inline void log_raw()
{
    // End of template recursion
}

extern "C" unsigned long millis();

// Variadic template for printing multiple items
template<typename T, typename T2, typename... Rest>
inline void log_raw(T first, T2 second, Rest... rest)
{
    log_raw(first);
    log_raw(second);
    log_raw(rest...);
}

// Format a complete log message
template<typename... Params>
inline void logmsg(Params... params)
{
    log_raw("[", (int)millis(), "ms] ");
    log_raw(params...);
    log_raw("\r\n");
}

// Format a complete debug message
template<typename... Params>
inline bool dbgmsg(Params... params)
{
    if (g_log_debug)
    {
        log_raw("[", (int)millis(), "ms] DBG ");
        log_raw(params...);
        log_raw("\r\n");
    }
    return g_log_debug;
}
