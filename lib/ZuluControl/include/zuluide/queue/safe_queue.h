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
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
**/
#pragma once

#include <stdint.h>
#ifdef CONTROL_CROSS_CORE_QUEUE
#include <pico/util/queue.h>
#else
#include <queue>
#endif
namespace zuluide::queue
{
    // The purpose of the this class is to provide a cross core mechanism 
    // for sending data between the two cores when CONTROL_CROSS_CORE_QUEUE
    // is defined at compile time
    // Otherwise fall back to a single core queue without the need for 
    // protections from parallel processing
    
    class SafeQueue {
        public:
            // Remove item from queue
            // \param data should be the address of a pointer (pointer to a pointer)
            //   child pointer will be set to the item in the queue
            // \returns true if an item was popped, false otherwise
            bool TryRemove(void *data);

            // Add item to queue, 
            // \param data should be the address of a pointer (pointer to a pointer)
            // \returns true the item could be added, false otherwise
            bool TryAdd(void *data);

            // Initialize the queue
            // \param element_size should be the size of object be transferred
            // \param element_count is the maximum length of the queue
            void Reset(uint32_t element_size, uint32_t element_count);

            // Get the current number of element in the queue
            uint32_t GetLevel();

        private:
#ifdef CONTROL_CROSS_CORE_QUEUE
        queue_t m_queue;
#else
        std::queue<void *> m_queue;
#endif

    };
}
