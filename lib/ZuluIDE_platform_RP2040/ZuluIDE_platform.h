// Platform-specific definitions for ZuluSCSI RP2040 hardware.

#pragma once

#include <stdint.h>
#include <Arduino.h>
#include "ZuluIDE_platform_gpio.h"

/* These are used in debug output and default SCSI strings */
extern const char *g_azplatform_name;
#define PLATFORM_NAME "ZuluIDE RP2040"
#define PLATFORM_REVISION "1.0"
#define SD_USE_SDIO 1

// Debug logging function, can be used to print to e.g. serial port.
// May get called from interrupt handlers.
void azplatform_log(const char *s);
void azplatform_emergency_log_save();

// Timing and delay functions.
// Arduino platform already provides these
// unsigned long millis(void);
// void delay(unsigned long ms);
// void delayMicroseconds(unsigned long us);

// Initialize SD card and GPIO configuration
void azplatform_init();

// Initialization for main application, not used for bootloader
void azplatform_late_init();

// Write the status LED through the mux
void azplatform_write_led(bool state);
#define LED_ON() azplatform_write_led(true)
#define LED_OFF() azplatform_write_led(false)

// Disable the status LED
void azplatform_disable_led(void);

// Query whether initiator mode is enabled on targets with PLATFORM_HAS_INITIATOR_MODE
bool azplatform_is_initiator_mode_enabled();

// Setup soft watchdog if supported
void azplatform_reset_watchdog();

// Set callback that will be called during data transfer to/from SD card.
// This can be used to implement simultaneous transfer to SCSI bus.
typedef void (*sd_callback_t)(uint32_t bytes_complete);
void azplatform_set_sd_callback(sd_callback_t func, const uint8_t *buffer);

// Reprogram firmware in main program area.
#ifndef RP2040_DISABLE_BOOTLOADER
#define AZPLATFORM_BOOTLOADER_SIZE (128 * 1024)
#define AZPLATFORM_FLASH_TOTAL_SIZE (1024 * 1024)
#define AZPLATFORM_FLASH_PAGE_SIZE 4096
bool azplatform_rewrite_flash_page(uint32_t offset, uint8_t buffer[AZPLATFORM_FLASH_PAGE_SIZE]);
void azplatform_boot_to_main_firmware();
#endif

// SD card driver for SdFat
class SdioConfig;
extern SdioConfig g_sd_sdio_config;
#define SD_CONFIG g_sd_sdio_config
#define SD_CONFIG_CRASH g_sd_sdio_config
