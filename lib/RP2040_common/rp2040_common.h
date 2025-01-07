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

// This is coded is the bare minimum required for the bootloader and common with the main RP2040 code
#pragma once

#include <stdint.h>
#include <hardware/sync.h>
#include <pico/mutex.h>
#include <hardware/gpio.h>

// Initialize SD card and GPIO configuration
void platform_init();

// Query IDE device id 0/1 requested on hardware DIP switches.
// If platform has no DIP switches, returns 0.
int platform_get_device_id(void);

// Setup soft watchdog if supported
void platform_reset_watchdog();

void platform_write_led(bool state);
#define LED_ON()  platform_write_led(true)
#define LED_OFF() platform_write_led(false)
void platform_set_blink_status(bool status);
// Controls the LED even if the LED is in a blinking state
void platform_write_led_override(bool state);
#define LED_ON_OVERRIDE()  platform_write_led_override(true)
#define LED_OFF_OVERRIDE()  platform_write_led_override(false)

// Disable the status LED
void platform_disable_led(void);

// Reprogram firmware in main program area.
#define PLATFORM_FLASH_TOTAL_SIZE (1020 * 1024)
#define PLATFORM_FLASH_PAGE_SIZE 4096
#ifndef RP2040_DISABLE_BOOTLOADER
#define PLATFORM_BOOTLOADER_SIZE (128 * 1024)
bool platform_rewrite_flash_page(uint32_t offset, uint8_t buffer[PLATFORM_FLASH_PAGE_SIZE]);
void platform_boot_to_main_firmware();
#endif

// Debug logging function, can be used to print to e.g. serial port.
// May get called from interrupt handlers.
void platform_log(const char *s);

void platform_emergency_log_save();

// SD card driver for SdFat
class SdioConfig;
extern SdioConfig g_sd_sdio_config;
#define SD_CONFIG g_sd_sdio_config
#define SD_CONFIG_CRASH g_sd_sdio_config


/**
   This mutex is used to prevent saving the log file to the SD card while reading the file system.
   A more robust file access method is needed, but this is fixing the problem for now, even though
   it is rather ham-handed.
 */
mutex_t* platform_get_log_mutex();

// Helper function for setting gpios
void gpio_conf(uint gpio, gpio_function_t fn, bool pullup, bool pulldown, bool output, bool initial_state, bool fast_slew);