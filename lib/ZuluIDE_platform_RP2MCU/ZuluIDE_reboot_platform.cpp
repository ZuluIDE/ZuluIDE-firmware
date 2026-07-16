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
#include <ZuluIDE_config.h>
#include <ZuluIDE_platform.h>
#include <ZuluIDE_usb_platform.h>
#include "ZuluControl_platform.h"
#ifdef ENABLE_AUDIO_OUTPUT
#include "audio.h"
#endif
#include "ZuluIDE_reboot_platform.h"


static reboot_cmd_t g_reboot_cmd = reboot_cmd_t::NONE;
extern SdFat SD;


static void pre_reboot_cleanup()
{
    ide_phy_stop_transfers();
    #ifdef ENABLE_AUDIO_OUTPUT
        audio_disable();
    #endif
    platform_close_i2c();
}

static void platform_reset_mcu_msc()
{
    watchdog_hw->scratch[0] = REBOOT_INTO_MSC_MAGIC_NUM;
    watchdog_reboot(0, 0, 0);
}

void platform_reset_mcu()
{
    watchdog_hw->scratch[0] = REBOOT_INTO_STD_MAGIC_NUM;
    watchdog_reboot(0, 0, 0);
}

bool platform_rebooted_into_msc()
{
    return (watchdog_hw->scratch[0] == REBOOT_INTO_MSC_MAGIC_NUM);
}

bool platform_rebooted_standard()
{
    return (watchdog_hw->scratch[0] == REBOOT_INTO_STD_MAGIC_NUM);
}

// Start the reboot process
void platform_start_reboot(reboot_cmd_t mode)
{
    g_reboot_cmd = mode;
}


// Called in ZuluIDE_platform.cpp as extern to avoid circular include
void platform_reboot_poll()
{
    if (g_reboot_cmd == reboot_cmd_t::NONE)
        return;
    pre_reboot_cleanup();

    uint32_t start_time = millis();
    while ((uint32_t)(millis() - start_time) < REBOOT_WAIT_TIME)
    {
        usb_log_poll();
        platform_reset_watchdog();
    }

    switch (g_reboot_cmd)
    {
        case reboot_cmd_t::NORMAL:
            platform_reset_mcu();
            break;
        case reboot_cmd_t::MSC:
            platform_reset_mcu_msc();
            break;
        case reboot_cmd_t::UF2:
            watchdog_hw->scratch[0] = REBOOT_INTO_STD_MAGIC_NUM;
            platform_reset_mcu_uf2();
            break;
        default:
            assert(false);
    }

    while(true) tight_loop_contents();
}