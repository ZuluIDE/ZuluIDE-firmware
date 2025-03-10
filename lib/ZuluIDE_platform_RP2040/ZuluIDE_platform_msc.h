/**
 * Copyright (c) 2023-2024 zigzagjoe
 * 
 * ZuluSCSI™ firmware is licensed under the GPL version 3 or any later version. 
 * Changed for use with ZuluIDE firmware.
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

#ifdef PLATFORM_MASS_STORAGE
#pragma once

// private constants/enums
#define SD_SECTOR_SIZE 512

/* return true if USB presence detected / eligble to enter CR mode */
bool platform_sense_msc();

/* perform MSC-specific init tasks */
void platform_enter_msc();

/* return true if we should remain in card reader mode. called in a loop. */
bool platform_run_msc();

/* perform any cleanup tasks for the MSC-specific functionality */
void platform_exit_msc();

#endif