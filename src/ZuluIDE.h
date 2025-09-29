// Common functions and global state available to all modules

#pragma once

#include "ZuluIDE_platform.h"
#include "ZuluIDE_log.h"
#include <SdFat.h>

extern SdFs SD;
extern bool g_sdcard_present;

// Checks if SD card is still present
bool poll_sd_card();

struct global_settings_t
{
    bool udma_enable_auto_switch;
};

extern global_settings_t g_settings;