// Platform-specific definitions for ZuluSCSI RP2040 hardware.

#pragma once

#include <stdint.h>
#include <Arduino.h>
#include "ZuluIDE_platform_gpio.h"

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
#define LED_ON() platform_write_led(true)
#define LED_OFF() platform_write_led(false)

// Disable the status LED
void platform_disable_led(void);

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

// FPGA bitstream is protected by a license key stored in RP2040 flash,
// in the last page before 1 MB boundary.
#define PLATFORM_LICENSE_KEY_OFFSET 0x000ff000
#define PLATFORM_LICENSE_KEY_ADDR ((const uint8_t*)(0x130ff000))

// Reprogram firmware in main program area.
#ifndef RP2040_DISABLE_BOOTLOADER
#define PLATFORM_BOOTLOADER_SIZE (128 * 1024)
#define PLATFORM_FLASH_TOTAL_SIZE (1020 * 1024)
#define PLATFORM_FLASH_PAGE_SIZE 4096
bool platform_rewrite_flash_page(uint32_t offset, uint8_t buffer[PLATFORM_FLASH_PAGE_SIZE]);
void platform_boot_to_main_firmware();
#endif

// SD card driver for SdFat
class SdioConfig;
extern SdioConfig g_sd_sdio_config;
#define SD_CONFIG g_sd_sdio_config
#define SD_CONFIG_CRASH g_sd_sdio_config
