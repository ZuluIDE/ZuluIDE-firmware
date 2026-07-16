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

#include "ZuluIDE_usb_console.h"
#include "ZuluIDE_platform.h"
#include "ZuluIDE_config.h"
#include "ZuluIDE_log.h"
#include <ZuluIDE_reboot_platform.h>
#include <zuluide/images/image_iterator.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// How long to wait while the serial TX FIFO is full before giving up
#ifndef IDE_CONSOLE_SERIAL_TIMEOUT_MS
constexpr uint32_t k_serial_timeout_ms = 30;
#else
constexpr uint32_t k_serial_timeout_ms = IDE_CONSOLE_SERIAL_TIMEOUT_MS;
#endif

// Check if all log data has been sent over USB serial
static bool log_buffer_is_empty()
{
    return platform_usb_log_is_flushed();
}

// -----------------------------------------------------------------------
// Bridge functions implemented in ZuluIDE.cpp
// Forward-declared here to avoid a cross-layer header dependency.
// -----------------------------------------------------------------------
extern int        zuluide_console_device_count();
extern const char *zuluide_console_device_type_name(int dev_idx);
extern bool       zuluide_console_is_removable(int dev_idx);
extern bool       zuluide_console_get_image(int dev_idx, char *buf, size_t buflen);
extern void       zuluide_console_load_image(int dev_idx, const char *path);
extern void       zuluide_console_eject(int dev_idx);
extern bool       zuluide_console_insert(int dev_idx);
extern bool       zuluide_console_load_next(int dev_idx);

// -----------------------------------------------------------------------
// Direct serial output — bypasses the log buffer so menu text never
// appears in zululog.txt.
// -----------------------------------------------------------------------

static void serial_out(const char *str)
{
    if (!str || !*str) return;
    if (!log_buffer_is_empty()) return;
    uint32_t remaining = static_cast<uint32_t>(strlen(str));
    const uint8_t *p = reinterpret_cast<const uint8_t *>(str);
    uint32_t timeout_start = millis();

    while ((static_cast<uint32_t>(millis() - timeout_start) < k_serial_timeout_ms) && remaining > 0)
    {
        uint32_t sent = platform_serial_write(const_cast<uint8_t *>(p), remaining);
        if (sent > 0)
            timeout_start = millis();
        p += sent;
        remaining -= sent;
        platform_reset_watchdog();
    }
}

static void serial_out_int(int value)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", value);
    serial_out(buf);
}

static void serial_println(const char *str)
{
    serial_out(str);
    serial_out("\r\n");
}

// -----------------------------------------------------------------------
// State machine
// -----------------------------------------------------------------------

enum class MenuState : uint8_t
{
    Inactive,                 // No menu active
    MainMenu,                 // Main menu with media selection and debug toggle
    MainMenuMediaConfirm,     // Waiting for 'y' confirmation on media menu entry
    MainMenuDebugConfirm,     // Waiting for 'y' confirmation on debug toggle
    MainMenuRebootConfirm,    // Waiting for 'y' confirmation on normal reboot
    MainMenuUF2Confirm,       // Waiting for 'y' confirmation on UF2 bootloader reboot
    MainMenuMSCConfirm,       // Waiting for 'y' confirmation on USB SD card reader reboot
    MainMenuExitMSCConfirm,   // Waiting for 'y' confirmation on exiting USB SD card reader mode
    ImageSelection,           // Image list and selection for primary device
    ImageSelectionList,       // Browsing enumerated images
};

static MenuState s_state            = MenuState::Inactive;
static bool      s_menu_just_entered = false; // show menu once then re-process key
static int       s_image_cnt        = 0;    // cached image count for ImageSelectionList
static char      s_num_buf[4]       = {};   // digit accumulator for image selection
static uint8_t   s_num_len          = 0;

// -----------------------------------------------------------------------
// Display helpers
// -----------------------------------------------------------------------

static void show_main_menu()
{
    serial_println("");
    serial_println("  ZuluIDE Main Menu");
    serial_println("  ================================================");
#ifdef PLATFORM_MASS_STORAGE
    if (!platform_in_msc_mode())
#endif
        serial_println("    'm' - Media Menu");
    serial_out("    'd' - debug logging  [");
    serial_out(g_log_debug ? "ON" : "OFF");
    serial_println("]");
#ifdef PLATFORM_MASS_STORAGE
    if (!platform_in_msc_mode())
    {
#endif
        serial_println("    'r' - reboot");
        serial_println("    'u' - reboot into UF2 bootloader");
#ifdef PLATFORM_MASS_STORAGE
    }
    if (!platform_in_msc_mode())
        serial_println("    's' - reboot into USB SD card reader mode");
    else
        serial_println("    'x' - exit USB SD card reader mode");
#endif
    serial_println("  ================================================");
}

static void show_image_selection()
{
    char cur[MAX_FILE_PATH];
    const bool has_img = zuluide_console_get_image(0, cur, sizeof(cur));
    const char *basename = "(ejected)";
    if (has_img)
    {
        const char *sl = strrchr(cur, '/');
        basename = sl ? sl + 1 : cur;
    }

    serial_println("\n");
    serial_out("  Device: ");
    serial_out(zuluide_console_device_type_name(0));
    serial_out("  |  Current: ");
    serial_println(basename);
    serial_println("  ================================================");
    serial_println("    'l' - list and select image");
    serial_println("    'n' - load next image in sequence");
    if (zuluide_console_is_removable(0))
    {
        serial_println("    'e' - eject media");
        serial_println("    'i' - insert (re-insert current image)");
    }
    serial_println("    'b' - back to main menu");
    serial_println("    'q' - exit menu");
    serial_println("  Or type image number + Enter to load directly:");
}

// Enumerate images via ImageIterator, print each one, return count.
//
// ImageIterator::MoveNext() on a freshly-constructed iterator (candidate == "")
// auto-calls Reset() which reopens the SD root.  MoveFirst() requires root to
// already be open and therefore fails after any prior Cleanup().  Using only
// MoveNext() is the correct pattern for images in the root directory.
static int show_image_selection_list()
{
    serial_println("");
    serial_out("  Available images (");
    serial_out(zuluide_console_device_type_name(0));
    serial_println("):");
    serial_println("  ------------------------------------------------");

    zuluide::images::ImageIterator iter;
    int count = 0;

    while (iter.MoveNext())
    {
        count++;
        const auto &img    = iter.Get();
        const char *fname  = img.GetFilename().c_str();
        // Filenames for root-directory images have no '/' prefix; strrchr
        // returns nullptr in that case, so fall back to the bare name.
        const char *base   = strrchr(fname, '/');
        base = base ? base + 1 : fname;
        const uint32_t sz_mb = static_cast<uint32_t>(
            (img.GetFileSizeBytes() + 524288ULL) / 1048576ULL);

        serial_out("    ");
        serial_out_int(count);
        serial_out(": ");
        serial_out(base);
        serial_out(" (");
        serial_out_int(static_cast<int>(sz_mb));
        serial_println(" MB)");
    }

    iter.Cleanup();

    if (count == 0)
        serial_println("    (no images found)");

    serial_println("  ------------------------------------------------");
    serial_println("  Enter number + Enter to load, 'b' to cancel, or 'q' to exit menu:");
    return count;
}

// Walk the iterator to the 1-based index and copy the filename.
// The iterator returns bare filenames (e.g. "image.iso") for root-directory
// files; that is the exact format open_file() and StatusController expect.
static bool get_path_by_index(int idx_1based, char *path, size_t pathlen)
{
    zuluide::images::ImageIterator iter;
    bool found = false;
    int i = 0;

    while (iter.MoveNext())
    {
        if (++i == idx_1based)
        {
            strncpy(path, iter.Get().GetFilename().c_str(), pathlen - 1);
            path[pathlen - 1] = '\0';
            found = true;
            break;
        }
    }

    iter.Cleanup();
    return found;
}

// Try to load the image at index idx_1based; emit feedback on serial.
static void do_load_by_index(int idx_1based)
{
    char path[MAX_FILE_PATH];
    if (!get_path_by_index(idx_1based, path, sizeof(path)))
    {
        logmsg("Image ", idx_1based, " not found.");
        return;
    }

    const char *base = strrchr(path, '/');
    const char *filename = base ? base + 1 : path;
    logmsg("Loading '", filename, "' ...");
    zuluide_console_load_image(0, path);
    logmsg("Done - host will see a media change.");
}

// -----------------------------------------------------------------------
// Public interface
// -----------------------------------------------------------------------

bool ideConsoleMenuActive()
{
    return s_state != MenuState::Inactive;
}

void ideConsoleMenuEnter()
{
    s_num_len          = 0;
    s_num_buf[0]       = '\0';
    s_image_cnt        = 0;
    s_state            = MenuState::MainMenu;
    s_menu_just_entered = true;
}

void ideConsoleMenuProcess(char c)
{
    switch (s_state)
    {
        // ----------------------------------------------------------------
        case MenuState::MainMenu:
        {
            bool just_entered = s_menu_just_entered;
            s_menu_just_entered = false;
            if (just_entered)
                show_main_menu();
            switch (c | 0x20)
            {
                case 'm':
#ifdef PLATFORM_MASS_STORAGE
                    if (platform_in_msc_mode())
                    {
                        show_main_menu();
                        break;
                    }
#endif
                    if (zuluide_console_device_count() == 0)
                    {
                        serial_println("  No IDE device configured.");
                        show_main_menu();
                    }
                    else
                    {
                        s_state = MenuState::MainMenuMediaConfirm;
                        serial_println("  Enter Media Menu? Press 'y' to confirm or any other key to cancel:");
                    }
                    break;

                case 'd':
                    s_state = MenuState::MainMenuDebugConfirm;
                    serial_out("  Toggle debug logging to ");
                    serial_out(g_log_debug ? "OFF" : "ON");
                    serial_println("? Press 'y' to confirm or any other key to cancel:");
                    break;

                case 'r':
#ifdef PLATFORM_MASS_STORAGE
                    if (platform_in_msc_mode())
                    {
                        show_main_menu();
                        break;
                    }
#endif
                    s_state = MenuState::MainMenuRebootConfirm;
                    serial_println("  Reboot? Press 'y' to confirm or any other key to cancel:");
                    break;

                case 'u':
#ifdef PLATFORM_MASS_STORAGE
                    if (platform_in_msc_mode())
                    {
                        show_main_menu();
                        break;
                    }
#endif
                    s_state = MenuState::MainMenuUF2Confirm;
                    serial_println("  Reboot into UF2 bootloader? Press 'y' to confirm or any other key to cancel:");
                    break;

#ifdef PLATFORM_MASS_STORAGE
                case 's':
                    if (!platform_in_msc_mode())
                    {
                        s_state = MenuState::MainMenuMSCConfirm;
                        serial_println("  Reboot into USB SD card reader mode? Press 'y' to confirm or any other key to cancel:");
                    }
                    else
                    {
                        show_main_menu();
                    }
                    break;

                case 'x':
                    if (platform_in_msc_mode())
                    {
                        s_state = MenuState::MainMenuExitMSCConfirm;
                        serial_println("  Exit USB SD card reader mode? Press 'y' to confirm or any other key to cancel:");
                    }
                    else
                    {
                        show_main_menu();
                    }
                    break;
#endif

                default:
                    if (!just_entered)
                        show_main_menu();
                    break;
            }
            break;
        }

        // ----------------------------------------------------------------
        case MenuState::MainMenuMediaConfirm:
        {
            if (c == 'y' || c == 'Y')
            {
                s_num_len    = 0;
                s_num_buf[0] = '\0';
                s_state      = MenuState::ImageSelection;
                show_image_selection();
            }
            else
            {
                s_state             = MenuState::MainMenu;
                s_menu_just_entered = true;
                ideConsoleMenuProcess(c);
            }
            break;
        }

        // ----------------------------------------------------------------
        case MenuState::MainMenuDebugConfirm:
        {
            if (c == 'y' || c == 'Y')
            {
                g_log_debug = !g_log_debug;
                logmsg("Debug logging ", g_log_debug ? "enabled." : "disabled.");
                s_state             = MenuState::MainMenu;
                s_menu_just_entered = true;
                ideConsoleMenuProcess(c);
            }
            else
            {
                s_state             = MenuState::MainMenu;
                s_menu_just_entered = true;
                ideConsoleMenuProcess(c);
            }
            break;
        }

        // ----------------------------------------------------------------
        case MenuState::MainMenuRebootConfirm:
        {
            if (c == 'y' || c == 'Y')
            {
                logmsg("Rebooting...");
                platform_start_reboot(reboot_cmd_t::NORMAL);
            }
            else
            {
                s_state             = MenuState::MainMenu;
                s_menu_just_entered = true;
                ideConsoleMenuProcess(c);
            }
            break;
        }

        // ----------------------------------------------------------------
        case MenuState::MainMenuUF2Confirm:
        {
            if (c == 'y' || c == 'Y')
            {
                logmsg("Rebooting into UF2 bootloader...");
                platform_start_reboot(reboot_cmd_t::UF2);
            }
            else
            {
                s_state             = MenuState::MainMenu;
                s_menu_just_entered = true;
                ideConsoleMenuProcess(c);
            }
            break;
        }

#ifdef PLATFORM_MASS_STORAGE
        // ----------------------------------------------------------------
        case MenuState::MainMenuMSCConfirm:
        {
            if (c == 'y' || c == 'Y')
            {
                logmsg("Rebooting into USB SD card reader mode...");
                platform_start_reboot(reboot_cmd_t::MSC);
            }
            else
            {
                s_state             = MenuState::MainMenu;
                s_menu_just_entered = true;
                ideConsoleMenuProcess(c);
            }
            break;
        }

        // ----------------------------------------------------------------
        case MenuState::MainMenuExitMSCConfirm:
        {
            if (c == 'y' || c == 'Y')
            {
                logmsg("Exiting USB SD card reader mode...");
                platform_request_msc_exit();
                s_state = MenuState::Inactive;
            }
            else
            {
                s_state             = MenuState::MainMenu;
                s_menu_just_entered = true;
                ideConsoleMenuProcess(c);
            }
            break;
        }
#endif

        // ----------------------------------------------------------------
        case MenuState::ImageSelection:
        {
            // Accumulate digits for direct image-number entry
            if (c >= '0' && c <= '9' && s_num_len < 3)
            {
                s_num_buf[s_num_len++] = c;
                s_num_buf[s_num_len]   = '\0';
                serial_out("  >> ");
                serial_out(s_num_buf);
                serial_println(" (Enter to load, 'b' to cancel)");
                break;
            }

            if ((c == '\r' || c == '\n') && s_num_len > 0)
            {
                const int idx = atoi(s_num_buf);
                s_num_len    = 0;
                s_num_buf[0] = '\0';
                do_load_by_index(idx);
                show_image_selection();
                break;
            }

            // Single-key actions (accept upper and lower case via bitwise OR)
            switch (c | 0x20)
            {
                case 'l':
                    s_num_len    = 0;
                    s_num_buf[0] = '\0';
                    s_state      = MenuState::ImageSelectionList;
                    s_image_cnt  = show_image_selection_list();
                    break;

                case 'n':
                    if (zuluide_console_load_next(0))
                        logmsg("Next image loaded - host will see media change.");
                    else
                        logmsg("No next image available.");
                    show_image_selection();
                    break;

                case 'e':
                    if (zuluide_console_is_removable(0))
                    {
                        zuluide_console_eject(0);
                        logmsg("Media ejected.");
                    }
                    else
                    {
                        logmsg("Device is not removable.");
                    }
                    show_image_selection();
                    break;

                case 'i':
                    if (zuluide_console_is_removable(0))
                    {
                        if (zuluide_console_insert(0))
                            logmsg("Media inserted.");
                        else
                            logmsg("No image loaded - use 'l' to select one.");
                    }
                    else
                    {
                        logmsg("Device is not removable.");
                    }
                    show_image_selection();
                    break;

                case 'b':
                    s_num_len    = 0;
                    s_num_buf[0] = '\0';
                    s_state      = MenuState::MainMenu;
                    show_main_menu();
                    break;

                case 'q':
                    logmsg("Exiting media menu.");
                    s_state = MenuState::MainMenu;
                    break;

                default:
                    s_num_len    = 0;
                    s_num_buf[0] = '\0';
                    show_image_selection();
                    break;
            }
            break;
        }

        // ----------------------------------------------------------------
        case MenuState::ImageSelectionList:
        {
            if (c == 'b' || c == 'B')
            {
                s_num_len    = 0;
                s_num_buf[0] = '\0';
                s_state      = MenuState::ImageSelection;
                show_image_selection();
                return;
            }

            if (c == 'q' || c == 'Q')
            {
                s_num_len    = 0;
                s_num_buf[0] = '\0';
                logmsg("Exiting media menu.");
                s_state      = MenuState::MainMenu;
                return;
            }

            if (c >= '0' && c <= '9' && s_num_len < 3)
            {
                s_num_buf[s_num_len++] = c;
                s_num_buf[s_num_len]   = '\0';
                serial_out("  >> ");
                serial_out(s_num_buf);
                serial_println(" (Enter to load, 'b' to cancel)");
                return;
            }

            if ((c == '\r' || c == '\n') && s_num_len > 0)
            {
                const int idx = atoi(s_num_buf);
                s_num_len    = 0;
                s_num_buf[0] = '\0';

                if (idx < 1 || idx > s_image_cnt)
                {
                    logmsg("Invalid index ", idx, " - valid range is 1 to ", s_image_cnt, ".");
                    serial_println("  Enter number + Enter, or 'b' to cancel:");
                    return;
                }

                do_load_by_index(idx);
                s_state = MenuState::ImageSelection;
                show_image_selection();
                return;
            }

            // Any other key: re-show prompt
            serial_println("  Enter number + Enter, or 'b' to cancel:");
            break;
        }

        // ----------------------------------------------------------------
        default:
            s_state = MenuState::Inactive;
            break;
    }
}
