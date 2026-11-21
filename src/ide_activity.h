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

// Tracks IDE data transfer throughput for each device and for both combined.
//
// "Read" is data sent from the device to the host (host reads the image),
// "write" is data received from the host (host writes the image), and
// "combined" is read + write.  Rates are in kB/s (1000 bytes per second).
//
// Byte counters are updated from the transfer paths on core 0.  poll() turns
// them into rates averaged over the output interval (zuluide.ini
// throughput_output_ms, 1 second default), shown by the USB console monitor.
// is_busy() tells the display and control code to pause or slow down UI work
// while data is moving.  The rates and activity time are 32-bit values so
// they can be read from either core.

#pragma once

#include <stdint.h>

class IDEActivity
{
public:
    static constexpr int NUM_DEVICES = 2;
    // Pass as dev_index to the getters for both devices combined
    static constexpr int ALL_DEVICES = NUM_DEVICES;

    static constexpr uint32_t DEFAULT_OUTPUT_UPDATE_MS = 1000;
    static constexpr uint32_t MIN_OUTPUT_UPDATE_MS = 100;
    static constexpr uint32_t MAX_UPDATE_MS = 60000;

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

    // Interval over which rates are averaged, clamped to MIN_OUTPUT_UPDATE_MS..MAX_UPDATE_MS
    void set_output_interval_ms(uint32_t interval_ms);
    uint32_t output_interval_ms() const { return m_interval_ms; }

    // Track activity and recalculate rates once the output interval has elapsed.
    // Must be called regularly from core 0.
    void poll();

    // Latest rates in kB/s. dev_index is 0, 1 or ALL_DEVICES.
    uint32_t read_kBps(int dev_index = ALL_DEVICES) const;
    uint32_t write_kBps(int dev_index = ALL_DEVICES) const;
    uint32_t combined_kBps(int dev_index = ALL_DEVICES) const;

    // Incremented each time poll() computes new rates
    uint32_t sample_count() const { return m_sample_count; }

    // True if any data was transferred within the last hold_ms, with poll() resolution.
    // Unlike the rates this reacts at once and also catches hosts doing small,
    // latency sensitive transfers, so it suits throttling the UI.
    bool is_busy(uint32_t hold_ms = DEFAULT_BUSY_HOLD_MS) const;

private:
    void update_rates(uint32_t now);

    uint64_t m_read_bytes[NUM_DEVICES];
    uint64_t m_write_bytes[NUM_DEVICES];
    uint64_t m_prev_read_bytes[NUM_DEVICES];
    uint64_t m_prev_write_bytes[NUM_DEVICES];

    volatile uint32_t m_read_kBps[NUM_DEVICES + 1];
    volatile uint32_t m_write_kBps[NUM_DEVICES + 1];
    volatile uint32_t m_sample_count;

    uint32_t m_interval_ms;
    uint32_t m_last_sample_ms;

    uint64_t m_activity_bytes;
    volatile uint32_t m_last_activity_ms;
    volatile bool m_activity_seen;
};

extern IDEActivity g_ide_activity;
