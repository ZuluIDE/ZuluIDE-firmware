/**
 * ZuluIDE™ - Copyright (c) 2026 Rabbit Hole Computing™
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Under Section 7 of GPL version 3, you are granted additional
 * permissions described in the ZuluIDE Hardware Support Library Exception
 * (GPL-3.0_HSL_Exception.md), as published by Rabbit Hole Computing™.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
**/

// Tracks IDE data transfer activity for each device.
//
// "Read" is data sent from the device to the host (host reads the image) and
// "write" is data received from the host (host writes the image).
//
// Byte counters are updated from the transfer paths on core 0.  poll()
// records when they last changed and is_busy() tells the display and control
// code to pause or slow down UI work while data is moving.  The activity time
// is a 32-bit value so it can be read from either core.

#pragma once

#include <stdint.h>

class IDEActivity
{
public:
    static constexpr int NUM_DEVICES = 2;

    // How long is_busy() stays true after the last transferred data
    static constexpr uint32_t DEFAULT_BUSY_HOLD_MS = 500;

    IDEActivity();

    // Count bytes transferred, called from the data transfer paths
    inline void add_read(int dev_index, uint32_t bytes)
    {
        if ((unsigned)dev_index < NUM_DEVICES) m_read_bytes[dev_index] += bytes;
    }

    inline void add_write(int dev_index, uint32_t bytes)
    {
        if ((unsigned)dev_index < NUM_DEVICES) m_write_bytes[dev_index] += bytes;
    }

    // Track activity. Must be called regularly from core 0.
    void poll();

    // True if any data was transferred within the last hold_ms, with poll() resolution.
    // Reacts at once and also catches hosts doing small, latency sensitive
    // transfers, so it suits throttling the UI.
    bool is_busy(uint32_t hold_ms = DEFAULT_BUSY_HOLD_MS) const;

private:
    uint64_t m_read_bytes[NUM_DEVICES];
    uint64_t m_write_bytes[NUM_DEVICES];

    uint64_t m_activity_bytes;
    volatile uint32_t m_last_activity_ms;
    volatile bool m_activity_seen;
};

extern IDEActivity g_ide_activity;
