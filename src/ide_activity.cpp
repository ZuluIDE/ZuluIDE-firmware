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

#include "ide_activity.h"
#include "ZuluIDE_platform.h"

IDEActivity g_ide_activity;

IDEActivity::IDEActivity():
    m_read_bytes(), m_write_bytes(), m_prev_read_bytes(), m_prev_write_bytes(),
    m_read_kBps(), m_write_kBps(), m_sample_count(0),
    m_interval_ms(DEFAULT_OUTPUT_UPDATE_MS), m_last_sample_ms(0),
    m_activity_bytes(0), m_last_activity_ms(0), m_activity_seen(false)
{
}

void IDEActivity::set_output_interval_ms(uint32_t interval_ms)
{
    if (interval_ms < MIN_OUTPUT_UPDATE_MS) interval_ms = MIN_OUTPUT_UPDATE_MS;
    if (interval_ms > MAX_UPDATE_MS) interval_ms = MAX_UPDATE_MS;
    m_interval_ms = interval_ms;
}

void IDEActivity::poll()
{
    uint32_t now = millis();

    // Detect activity here instead of timestamping in add_read()/add_write(),
    // keeping the transfer path to a single add
    uint64_t total = 0;
    for (int i = 0; i < NUM_DEVICES; i++)
    {
        total += m_read_bytes[i] + m_write_bytes[i];
    }
    if (total != m_activity_bytes)
    {
        m_activity_bytes = total;
        m_last_activity_ms = now;
        m_activity_seen = true;
    }

    update_rates(now);
}

bool IDEActivity::is_busy(uint32_t hold_ms) const
{
    return m_activity_seen && (uint32_t)(millis() - m_last_activity_ms) < hold_ms;
}

void IDEActivity::update_rates(uint32_t now)
{
    uint32_t elapsed = now - m_last_sample_ms;
    if (elapsed < m_interval_ms)
        return;

    uint32_t total_read = 0;
    uint32_t total_write = 0;
    for (int i = 0; i < NUM_DEVICES; i++)
    {
        uint64_t read_delta = m_read_bytes[i] - m_prev_read_bytes[i];
        uint64_t write_delta = m_write_bytes[i] - m_prev_write_bytes[i];
        m_prev_read_bytes[i] = m_read_bytes[i];
        m_prev_write_bytes[i] = m_write_bytes[i];

        // Bytes per millisecond equals kB/s
        m_read_kBps[i] = (uint32_t)(read_delta / elapsed);
        m_write_kBps[i] = (uint32_t)(write_delta / elapsed);
        total_read += m_read_kBps[i];
        total_write += m_write_kBps[i];
    }
    m_read_kBps[ALL_DEVICES] = total_read;
    m_write_kBps[ALL_DEVICES] = total_write;

    m_last_sample_ms = now;
    m_sample_count++;
}

uint32_t IDEActivity::read_kBps(int dev_index) const
{
    if ((unsigned)dev_index > ALL_DEVICES) return 0;
    return m_read_kBps[dev_index];
}

uint32_t IDEActivity::write_kBps(int dev_index) const
{
    if ((unsigned)dev_index > ALL_DEVICES) return 0;
    return m_write_kBps[dev_index];
}

uint32_t IDEActivity::combined_kBps(int dev_index) const
{
    return read_kBps(dev_index) + write_kBps(dev_index);
}
