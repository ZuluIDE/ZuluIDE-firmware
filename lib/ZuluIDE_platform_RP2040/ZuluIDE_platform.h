/**
 * ZuluIDE™ - Copyright (c) 2023 Rabbit Hole Computing™
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

// Platform-specific definitions for ZuluSCSI RP2040 hardware.

#pragma once

#include <stdint.h>
#include <Arduino.h>
#include "ZuluIDE_platform_gpio.h"
#include <Wire.h>
#include <zuluide/observable.h>
#include <zuluide/observable_safe.h>
#include <zuluide/control/input_interface.h>
#include <zuluide/control/display_state.h>
#include <zuluide/status/system_status.h>
#include <zuluide/status/device_control_safe.h>

#include <pico/util/queue.h>

/* These are used in debug output and default SCSI strings */
extern const char *g_platform_name;
#define PLATFORM_NAME "ZuluIDE RP2040"
#define PLATFORM_REVISION "1.0"
#define SD_USE_SDIO 1

#ifndef PLATFORM_VDD_WARNING_LIMIT_mV
#define PLATFORM_VDD_WARNING_LIMIT_mV 3000
#endif

// Debug logging function, can be used to print to e.g. serial port.
// May get called from interrupt handlers.
void platform_log(const char *s);
void platform_emergency_log_save();

// Timing and delay functions.
// Arduino platform already provides these
// unsigned long millis(void);
// void delay(unsigned long ms);
// void delayMicroseconds(unsigned long us);

// Initialize SD card and GPIO configuration
void platform_init();

// Initialization for main application, not used for bootloader
void platform_late_init();

// Write the status LED through the mux
void platform_write_led(bool state);
#define LED_ON()  platform_write_led(true)
#define LED_OFF() platform_write_led(false)
void platform_set_blink_status(bool status);
void platform_write_led_override(bool state);
#define LED_ON_OVERRIDE()  platform_write_led_override(true)
#define LED_OFF_OVERRIDE()  platform_write_led_override(false)

// Disable the status LED
void platform_disable_led(void);

void platform_init_eject_button(uint8_t eject_button);

// Returns the state of any platform-specific buttons.
// The returned value should be a mask for buttons 1-8 in bits 0-7 respectively,
// where '1' is a button pressed and '0' is a button released.
// Debouncing logic is left up to the specific implementation.
// This function should return without significantly delay.
uint8_t platform_get_buttons();

// Query IDE device id 0/1 requested on hardware DIP switches.
// If platform has no DIP switches, returns 0.
int platform_get_device_id(void);

// Setup soft watchdog if supported
void platform_reset_watchdog();

// Poll function that is called every few milliseconds.
// The SD card is free to access during this time, and pauses up to
// few milliseconds shouldn't disturb SCSI communication.
void platform_poll();

// Set callback that will be called during data transfer to/from SD card.
// This can be used to implement simultaneous transfer to SCSI bus.
typedef void (*sd_callback_t)(uint32_t bytes_complete);
void platform_set_sd_callback(sd_callback_t func, const uint8_t *buffer);

/**
   Attempts to determine whether the hardware UI or the web service is attached to the device.
 */
bool platform_check_for_controller();

/**
   Sets the status controller, the component tracking the state of the system.
 */
void platform_set_status_controller(zuluide::ObservableSafe<zuluide::status::SystemStatus>& statusController);

/**
   Sets the display controller, the component tracking the state of the user interface.
 */
void platform_set_display_controller(zuluide::Observable<zuluide::control::DisplayState>& displayController);

/**
   Sets the controller that is used by the UI to change the system state.
 */
void platform_set_device_control(zuluide::status::DeviceControlSafe* deviceControl);

/**
   This mutex is used to prevent saving the log file to the SD card while reading the file system.
   A more robust file access method is needed, but this is fixing the problem for now, even though
   it is rather ham-handed.
 */
mutex_t* platform_get_log_mutex();

/**
   Sets the input receiver, which handles receiving input from the hardware UI and performs updates to the UI as appropriate.
 */
void platform_set_input_interface(zuluide::control::InputReceiver* inputReceiver);

/**
   Used to poll the input hardware.
 */
void platform_poll_input();

// FPGA bitstream is protected by a license key stored in RP2040 flash,
// in the last page before 1 MB boundary.
#define PLATFORM_LICENSE_KEY_OFFSET 0x000ff000
#define PLATFORM_LICENSE_KEY_ADDR ((const uint8_t*)(0x130ff000))


// Reprogram firmware in main program area.
#define PLATFORM_FLASH_TOTAL_SIZE (1020 * 1024)
#define PLATFORM_FLASH_PAGE_SIZE 4096
#ifndef RP2040_DISABLE_BOOTLOADER
#define PLATFORM_BOOTLOADER_SIZE (128 * 1024)
bool platform_rewrite_flash_page(uint32_t offset, uint8_t buffer[PLATFORM_FLASH_PAGE_SIZE]);
void platform_boot_to_main_firmware();
#endif


// SD card driver for SdFat
class SdioConfig;
extern SdioConfig g_sd_sdio_config;
#define SD_CONFIG g_sd_sdio_config
#define SD_CONFIG_CRASH g_sd_sdio_config
