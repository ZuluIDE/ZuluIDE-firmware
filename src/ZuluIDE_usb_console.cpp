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
    Inactive,
    DeviceList,    // multi-device: choose which IDE device to manage
    DeviceActions, // choose action for selected device
    ImageList,     // choose image from enumerated list
};

static MenuState s_state      = MenuState::Inactive;
static uint8_t   s_sel_dev    = 0;    // selected device index (0 or 1)
static int       s_image_cnt  = 0;    // cached image count for ImageList
static char      s_num_buf[4] = {};   // digit accumulator for image selection
static uint8_t   s_num_len    = 0;

// -----------------------------------------------------------------------
// Display helpers
// -----------------------------------------------------------------------

static void show_device_list()
{
    const int count = zuluide_console_device_count();
    serial_println("");
    serial_println("  ZuluIDE Media Management");
    serial_println("  ================================================");
    serial_println("  IDE devices:");
    for (int i = 0; i < count; i++)
    {
        char cur[MAX_FILE_PATH];
        const bool has_img = zuluide_console_get_image(i, cur, sizeof(cur));
        const char *basename = "(ejected)";
        if (has_img)
        {
            const char *sl = strrchr(cur, '/');
            basename = sl ? sl + 1 : cur;
        }
        serial_out("    ");
        serial_out_int(i);
        serial_out(": ");
        serial_out(zuluide_console_device_type_name(i));
        serial_out("  [");
        serial_out(basename);
        serial_println("]");
    }
    serial_println("  ------------------------------------------------");
    serial_out("  Select device (0");
    if (count > 1)
    {
        serial_out("-");
        serial_out_int(count - 1);
    }
    serial_println(") or 'q' to exit:");
}

static void show_device_actions()
{
    char cur[MAX_FILE_PATH];
    const bool has_img = zuluide_console_get_image(s_sel_dev, cur, sizeof(cur));
    const char *basename = "(ejected)";
    if (has_img)
    {
        const char *sl = strrchr(cur, '/');
        basename = sl ? sl + 1 : cur;
    }

    serial_println("");
    serial_out("  Device: ");
    serial_out(zuluide_console_device_type_name(s_sel_dev));
    serial_out("  |  Current: ");
    serial_println(basename);
    serial_println("  ================================================");
    serial_println("    'l' - list and select image");
    serial_println("    'n' - load next image in sequence");
    if (zuluide_console_is_removable(s_sel_dev))
    {
        serial_println("    'e' - eject media");
        serial_println("    'i' - insert (re-insert current image)");
    }
    serial_out("    'd' - toggle debug logging  [");
    serial_out(g_log_debug ? "ON" : "OFF");
    serial_println("]");
    if (zuluide_console_device_count() > 1)
        serial_println("    'b' - back to device list");
    serial_println("    'q' - exit menu");
    serial_println("  Or type image number + Enter to load directly:");
}

// Enumerate images via ImageIterator, print each one, return count.
//
// ImageIterator::MoveNext() on a freshly-constructed iterator (candidate == "")
// auto-calls Reset() which reopens the SD root.  MoveFirst() requires root to
// already be open and therefore fails after any prior Cleanup().  Using only
// MoveNext() is the correct pattern for images in the root directory.
static int show_image_list()
{
    serial_println("");
    serial_out("  Available images (");
    serial_out(zuluide_console_device_type_name(s_sel_dev));
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
    serial_println("  Enter number + Enter to load, or 'b' to cancel:");
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
        serial_out("  Image ");
        serial_out_int(idx_1based);
        serial_println(" not found.");
        return;
    }

    serial_out("  Loading '");
    const char *base = strrchr(path, '/');
    serial_out(base ? base + 1 : path);
    serial_println("' ...");
    zuluide_console_load_image(s_sel_dev, path);
    serial_println("  Done — host will see a media change.");
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
    s_num_len    = 0;
    s_num_buf[0] = '\0';
    s_image_cnt  = 0;

    const int count = zuluide_console_device_count();
    if (count == 0)
    {
        serial_println("\r\n  No IDE device configured.");
        s_state = MenuState::Inactive;
        return;
    }

    if (count > 1)
    {
        s_state = MenuState::DeviceList;
        show_device_list();
    }
    else
    {
        s_sel_dev = 0;
        s_state   = MenuState::DeviceActions;
        show_device_actions();
    }
}

void ideConsoleMenuProcess(char c)
{
    // Ignore lone CR/LF when no digit is pending
    if ((c == '\r' || c == '\n') && s_num_len == 0)
        return;

    switch (s_state)
    {
        // ----------------------------------------------------------------
        case MenuState::DeviceList:
        {
            if (c == 'q' || c == 'Q')
            {
                serial_println("  Exiting media menu.");
                s_state = MenuState::Inactive;
                return;
            }
            const int dev_idx = c - '0';
            if (c >= '0' && dev_idx < zuluide_console_device_count())
            {
                s_sel_dev = static_cast<uint8_t>(dev_idx);
                s_state   = MenuState::DeviceActions;
                show_device_actions();
                return;
            }
            show_device_list();
            break;
        }

        // ----------------------------------------------------------------
        case MenuState::DeviceActions:
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
                show_device_actions();
                break;
            }

            // Single-key actions (accept upper and lower case via bitwise OR)
            switch (c | 0x20)
            {
                case 'l':
                    s_num_len    = 0;
                    s_num_buf[0] = '\0';
                    s_state      = MenuState::ImageList;
                    s_image_cnt  = show_image_list();
                    break;

                case 'n':
                    if (zuluide_console_load_next(s_sel_dev))
                        serial_println("  Next image loaded — host will see media change.");
                    else
                        serial_println("  No next image available.");
                    show_device_actions();
                    break;

                case 'd':
                    g_log_debug = !g_log_debug;
                    serial_out("  Debug logging ");
                    serial_println(g_log_debug ? "enabled." : "disabled.");
                    show_device_actions();
                    break;

                case 'e':
                    if (zuluide_console_is_removable(s_sel_dev))
                    {
                        zuluide_console_eject(s_sel_dev);
                        serial_println("  Media ejected.");
                    }
                    else
                    {
                        serial_println("  Device is not removable.");
                    }
                    show_device_actions();
                    break;

                case 'i':
                    if (zuluide_console_is_removable(s_sel_dev))
                    {
                        if (zuluide_console_insert(s_sel_dev))
                            serial_println("  Media inserted.");
                        else
                            serial_println("  No image loaded — use 'l' to select one.");
                    }
                    else
                    {
                        serial_println("  Device is not removable.");
                    }
                    show_device_actions();
                    break;

                case 'b':
                    s_num_len    = 0;
                    s_num_buf[0] = '\0';
                    if (zuluide_console_device_count() > 1)
                    {
                        s_state = MenuState::DeviceList;
                        show_device_list();
                    }
                    else
                    {
                        serial_println("  Exiting media menu.");
                        s_state = MenuState::Inactive;
                    }
                    break;

                case 'q':
                    serial_println("  Exiting media menu.");
                    s_state = MenuState::Inactive;
                    break;

                default:
                    s_num_len    = 0;
                    s_num_buf[0] = '\0';
                    show_device_actions();
                    break;
            }
            break;
        }

        // ----------------------------------------------------------------
        case MenuState::ImageList:
        {
            if (c == 'b' || c == 'B')
            {
                s_num_len    = 0;
                s_num_buf[0] = '\0';
                s_state      = MenuState::DeviceActions;
                show_device_actions();
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
                    serial_out("  Invalid index ");
                    serial_out_int(idx);
                    serial_out(" — valid range is 1");
                    if (s_image_cnt > 1)
                    {
                        serial_out(" to ");
                        serial_out_int(s_image_cnt);
                    }
                    serial_println(".");
                    serial_println("  Enter number + Enter, or 'b' to cancel:");
                    return;
                }

                do_load_by_index(idx);
                s_state = MenuState::DeviceActions;
                show_device_actions();
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
