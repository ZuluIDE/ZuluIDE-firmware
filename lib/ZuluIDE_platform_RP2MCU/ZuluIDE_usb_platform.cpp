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


#include <ZuluIDE_usb_console.h>
#include <stdint.h>
#include <SerialUSB.h>
#include <ZuluIDE_log.h>
#include "ZuluIDE_usb_platform.h"

// Poll for commands sent through the USB serial port.
// When the interactive media menu is open every incoming byte is fed
// directly to the menu state machine.  Otherwise the existing line-buffer
// logic handles special commands (e.g. "license <key>")
void usb_command_poll()
{
    static uint8_t rx_buf[64];
    static int rx_len;

    uint32_t available = Serial.available();
    if (available > 0)
    {
        available = std::min<uint32_t>(available, sizeof(rx_buf) - rx_len);
        Serial.readBytes(rx_buf + rx_len, available);
        rx_len += available;
    }

    if (rx_len == 0) return;

    // Initial mode: Display the menu if any key but `L` or whitespace is pressed,
    // This is to enable accumulation of a full line for the usb_factory_command_handler()
    if (!ideConsoleMenuActive())
    {
        if (usb_has_factory_command_handler())
        {
            char *first = reinterpret_cast<char *>(rx_buf);
            bool trimmed = false;
            for (int i = 0; i < rx_len; i++)
            {
                char c = static_cast<char>(rx_buf[i]);

                while (!trimmed && isspace(static_cast<unsigned char>(c)))
                {
                    first++;
                    i++;
                    // If the input is all spaces, return
                    if (i >= rx_len)
                    {
                        rx_len = 0;
                        return;
                    }
                }

                if (!trimmed)
                    c = static_cast<char>(rx_buf[i]);
                trimmed = true;


                // Show main menu on first non-whitespace character (except 'l')
                if (*first != 'l' && *first != 'L')
                {
                    ideConsoleMenuEnter();
                    ideConsoleMenuProcess(static_cast<char>(rx_buf[i]));
                    rx_len = 0;
                    return;
                }

                if ((*first == 'l' || *first == 'L') && (c == '\n' || c == '\r'))
                {
                    rx_buf[i] = '\0';
                    usb_factory_command_handler(first);
                    rx_len = 0;
                    return;
                }
            }
        }
        else
        {
            ideConsoleMenuEnter();
        }
    }

    // When the interactive console is active, feed every byte directly
    // to its state machine and skip the line-buffer logic entirely.
    if (ideConsoleMenuActive())
    {
        for (int i = 0; i < rx_len; i++)
            ideConsoleMenuProcess(static_cast<char>(rx_buf[i]));
        rx_len = 0;
        return;
    }



    if (rx_len == sizeof(rx_buf))
    {
        rx_len = 0;
    }
}