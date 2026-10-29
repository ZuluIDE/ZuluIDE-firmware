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
    m_read_bytes(), m_write_bytes(),
    m_activity_bytes(0), m_last_activity_ms(0), m_activity_seen(false)
{
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
}

bool IDEActivity::is_busy(uint32_t hold_ms) const
{
    return m_activity_seen && (uint32_t)(millis() - m_last_activity_ms) < hold_ms;
}
