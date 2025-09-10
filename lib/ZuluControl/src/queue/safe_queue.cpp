/**
 * ZuluIDE™ - Copyright (c) 2025 Rabbit Hole Computing™
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

#include <zuluide/queue/safe_queue.h>
using namespace zuluide::queue;
#ifdef CONTROL_CROSS_CORE_QUEUE

bool SafeQueue::TryRemove(void *data)
{
    return queue_try_remove(&m_queue, data);
}
bool SafeQueue::TryAdd(void *data)
{
    return queue_try_add(&m_queue, data);
}
void SafeQueue::Reset(uint32_t element_size, uint32_t element_count)
{
    queue_init(&m_queue, element_size, element_count);
}

uint32_t SafeQueue::GetLevel()
{
    return queue_get_level(&m_queue);
}

#else

bool SafeQueue::TryRemove(void *data)
{
    if (!m_queue.empty())
    {
        *(void**)data = m_queue.front();
        m_queue.pop();
        return true;
    }
    return false;
}
bool SafeQueue::TryAdd(void *data)
{
    m_queue.push(*(void**)data);
    return true;
}
void SafeQueue::Reset(uint32_t element_size, uint32_t element_count)
{
    // Empty out queue
    while(!m_queue.empty()) m_queue.pop();
}

uint32_t SafeQueue::GetLevel()
{
    return m_queue.size();
}
#endif
