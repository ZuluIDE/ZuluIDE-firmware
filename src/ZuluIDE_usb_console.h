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

// USB serial console for interactive media management.
// Provides a text menu for listing, selecting, ejecting and inserting images
// on the IDE device(s).  Output goes directly to the serial port so it does
// not pollute zululog.txt.
// Driven by the platform's usb_command_poll() machinery.

#pragma once

// Returns true while the media menu is active.
// platform usb_command_poll() checks this to route characters here instead
// of the normal line-buffered command handler.
bool ideConsoleMenuActive();

// Feed one character received from the USB serial port into the menu.
// Only called when ideConsoleMenuActive() returns true.
void ideConsoleMenuProcess(char c);

// Enter the media management menu.
// Called by usb_command_poll() when the user presses 'm'.
void ideConsoleMenuEnter();
