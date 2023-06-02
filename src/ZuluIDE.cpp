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

#include <SdFat.h>
#include <minIni.h>
#include <strings.h>
#include "ZuluIDE_config.h"
#include "ZuluIDE_platform.h"
#include "ZuluIDE_log.h"
#include "ide_protocol.h"
#include "ide_cdrom.h"

/************************************/
/* Status reporting by blinking led */
/************************************/

#define BLINK_STATUS_OK 1
#define BLINK_ERROR_NO_IMAGES    3
#define BLINK_ERROR_NO_SD_CARD 5

void blinkStatus(int count)
{
    for (int i = 0; i < count; i++)
    {
        LED_ON();
        delay(250);
        LED_OFF();
        delay(250);
    }
}

/*********************************/
/* SD card mounting              */
/*********************************/

SdFs SD;
bool g_sdcard_present;

static bool mountSDCard()
{
    // Check for the common case, FAT filesystem as first partition
    if (SD.begin(SD_CONFIG))
        return true;

    // Do we have any kind of card?
    if (!SD.card() || SD.sdErrorCode() != 0)
        return false;

    // Try to mount the whole card as FAT (without partition table)
    if (static_cast<FsVolume*>(&SD)->begin(SD.card(), true, 0))
        return true;

    // Failed to mount FAT filesystem, but card can still be accessed as raw image
    return true;
}

void print_sd_info()
{
    uint64_t size = (uint64_t)SD.vol()->clusterCount() * SD.vol()->bytesPerCluster();
    logmsg("SD card detected, FAT", (int)SD.vol()->fatType(),
                    " volume size: ", (int)(size / 1024 / 1024), " MB");

    cid_t sd_cid;

    if(SD.card()->readCID(&sd_cid))
    {
        logmsg("SD MID: ", (uint8_t)sd_cid.mid, ", OID: ", (uint8_t)sd_cid.oid[0], " ", (uint8_t)sd_cid.oid[1]);

        char sdname[6] = {sd_cid.pnm[0], sd_cid.pnm[1], sd_cid.pnm[2], sd_cid.pnm[3], sd_cid.pnm[4], 0};
        logmsg("SD Name: ", sdname);
        logmsg("SD Date: ", (int)sd_cid.mdtMonth(), "/", sd_cid.mdtYear());
        logmsg("SD Serial: ", sd_cid.psn());
    }
}


/**************/
/* Log saving */
/**************/

FsFile g_logfile;

void save_logfile(bool always = false)
{
    static uint32_t prev_log_pos = 0;
    static uint32_t prev_log_len = 0;
    static uint32_t prev_log_save = 0;
    uint32_t loglen = log_get_buffer_len();

    if (loglen != prev_log_len && g_sdcard_present)
    {
        // Save log at most every LOG_SAVE_INTERVAL_MS
        if (always || (LOG_SAVE_INTERVAL_MS > 0 && (uint32_t)(millis() - prev_log_save) > LOG_SAVE_INTERVAL_MS))
        {
            g_logfile.write(log_get_buffer(&prev_log_pos));
            g_logfile.flush();

            prev_log_len = loglen;
            prev_log_save = millis();
        }
    }
}

void init_logfile()
{
    static bool first_open_after_boot = true;

    bool truncate = first_open_after_boot;
    int flags = O_WRONLY | O_CREAT | (truncate ? O_TRUNC : O_APPEND);
    g_logfile = SD.open(LOGFILE, flags);
    if (!g_logfile.isOpen())
    {
        logmsg("Failed to open log file: ", SD.sdErrorCode());
    }
    save_logfile(true);

    first_open_after_boot = false;
}


/*********************************/
/* Image file searching          */
/*********************************/

// Find the next image file in alphabetical order.
// If prev_image is NULL, returns the first image file.
static bool find_next_image(const char *directory, const char *prev_image, char *result, size_t buflen)
{
    FsFile root;
    FsFile file;

    if (!root.open(directory))
    {
        logmsg("Could not open directory: ", directory);
        return false;
    }

    result[0] = '\0';

    while (file.openNext(&root, O_RDONLY))
    {
        if (file.isDirectory()) continue;

        char candidate[MAX_FILE_PATH];
        file.getName(candidate, sizeof(candidate));
        const char *extension = strrchr(candidate, '.');

        if (strcasecmp(extension, ".iso") != 0)
        {
            // For now we only support ISO images
            continue;
        }

        if (!isalnum(candidate[0]))
        {
            // Skip names beginning with special character
            continue;
        }

        if (prev_image && strcasecmp(candidate, prev_image) <= 0)
        {
            // Alphabetically before the previous image
            continue;
        }

        if (result[0] && strcasecmp(candidate, result) >= 0)
        {
            // Alphabetically later than current result
            continue;
        }

        // Copy as the best result
        file.getName(result, buflen);
    }

    file.close();
    root.close();

    return result[0] != '\0';
}

/*********************************/
/* Main IDE handling loop        */
/*********************************/

static uint32_t g_ide_buffer[IDE_BUFFER_SIZE / 4];
static IDECDROMDevice g_ide_cdrom;
static IDEImageFile g_ide_imagefile;

void load_image()
{
    g_ide_cdrom.set_image(nullptr);
    g_ide_imagefile = IDEImageFile((uint8_t*)g_ide_buffer, sizeof(g_ide_buffer));
    
    char imagefile[MAX_FILE_PATH];
    if (find_next_image("/", NULL, imagefile, sizeof(imagefile)))
    {
        logmsg("Loading image ", imagefile);
        g_ide_imagefile.open_file(SD.vol(), imagefile, true);
        g_ide_cdrom.set_image(&g_ide_imagefile);
        blinkStatus(BLINK_STATUS_OK);
    }
    else
    {
        logmsg("No image files found");
        blinkStatus(BLINK_ERROR_NO_IMAGES);
    }
}

// TODO: Build bitstream into firmware instead of loading from SD card.
uint8_t fpga_bitstream[71337];

void zuluide_setup(void)
{
    platform_init();

    g_sdcard_present = mountSDCard();

    if(!g_sdcard_present)
    {
        logmsg("SD card init failed, sdErrorCode: ", (int)SD.sdErrorCode(),
                     " sdErrorData: ", (int)SD.sdErrorData());

        do
        {
            blinkStatus(BLINK_ERROR_NO_SD_CARD);
            delay(1000);
            platform_reset_watchdog();
            g_sdcard_present = mountSDCard();
        } while (!g_sdcard_present);
        logmsg("SD card init succeeded after retry");
    }

    if (g_sdcard_present)
    {
        if (SD.clusterCount() == 0)
        {
            logmsg("SD card without filesystem!");
        }

        print_sd_info();
    }

    FsFile bitstream = SD.open("ice5lp1k_top_bitmap.bin", O_RDONLY);
    if (!bitstream.isOpen())
    {
        logmsg("Could not find bitstream file ice5lp1k_top_bitmap.bin!");
        init_logfile();
        while (1)
        {
            blinkStatus(2);
            delay(2000);
        }
    }

    bitstream.read(fpga_bitstream, sizeof(fpga_bitstream));
    bitstream.close();
    logmsg("Loaded bitstream from ice5lp1k_top_bitmap.bin to RAM, beginning FPGA load");

    platform_late_init();

    load_image();
    logmsg("Initialization complete!");

    if (g_sdcard_present)
    {
        init_logfile();
        if (ini_getbool("IDE", "DisableStatusLED", false, CONFIGFILE))
        {
            platform_disable_led();
        }
    }

    ide_protocol_init(&g_ide_cdrom, NULL);
}

void zuluide_main_loop(void)
{
    static uint32_t sd_card_check_time = 0;

    platform_reset_watchdog();
    platform_poll();

    save_logfile();

    ide_protocol_poll();

    if (g_sdcard_present)
    {
        // Check SD card status for hotplug
        if ((uint32_t)(millis() - sd_card_check_time) > 5000)
        {
            sd_card_check_time = millis();
            uint32_t ocr;
            if (!SD.card()->readOCR(&ocr))
            {
                if (!SD.card()->readOCR(&ocr))
                {
                    g_sdcard_present = false;
                    logmsg("SD card removed, trying to reinit");
                }
            }
        }
    }

    if (!g_sdcard_present && (uint32_t)(millis() - sd_card_check_time) > 1000)
    {
        // Try to remount SD card
        g_sdcard_present = mountSDCard();

        if (g_sdcard_present)
        {
            logmsg("SD card reinit succeeded");
            print_sd_info();

            init_logfile();
            load_image();
            blinkStatus(BLINK_STATUS_OK);
        }
        else
        {
            blinkStatus(BLINK_ERROR_NO_SD_CARD);
        }

        sd_card_check_time = millis();
    }
}
