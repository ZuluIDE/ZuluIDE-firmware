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
#include "ZuluIDE.h"
#include "ZuluIDE_config.h"
#include "ZuluIDE_platform.h"
#include "ZuluIDE_log.h"
#include "ide_protocol.h"
#include "ide_cdrom.h"
#include "ide_zipdrive.h"
#include "ide_removable.h"
#include "ide_imagefile.h"

bool g_sdcard_present;
static FsFile g_logfile;

static uint32_t g_ide_buffer[IDE_BUFFER_SIZE / 4];

// Currently supports one IDE device
static IDECDROMDevice g_ide_cdrom;
static IDEZipDrive g_ide_zipdrive;
static IDERemovable g_ide_removable;
static IDEImageFile g_ide_imagefile;
static IDEDevice *g_ide_device;


/************************************/
/* Status reporting by blinking led */
/************************************/

#define BLINK_STATUS_OK 1
#define BLINK_ERROR_NO_IMAGES    3
#define BLINK_ERROR_NO_SD_CARD 5


static int g_blink_status_count = 0;
static bool g_blink_new_count = false;

// Handle LED blinking without delaying other processing
void blink_poll()
{
    static bool prev_state;
    static bool prev_phase;
    static int blink_status_code = -2;

    if (g_blink_new_count)
    {
        if (blink_status_code <= -2)
        {
            blink_status_code = g_blink_status_count;
            g_blink_new_count = false;
        }
    }

    bool phase = millis() & 256;

    if (blink_status_code > 0)
    {
        if (phase && !prev_phase)
        {
            LED_ON();
            prev_state = true;
        }
        else if (!phase && prev_phase)
        {
            LED_OFF();
            prev_state = false;
            blink_status_code -= 1;
        }
    }
    else if (blink_status_code > -2)
    {
        // Implement delay between blink codes
        if (!phase && prev_phase)
        {
            blink_status_code -= 1;
        }
    }
    else if (prev_state)
    {
        LED_OFF();
    }

    prev_phase = phase;
}

void blinkStatus(int count)
{
    g_blink_status_count = count;
    g_blink_new_count = true;

}

/*********************************/
/* SD card mounting              */
/*********************************/

static bool mountSDCard()
{
    // Verify that all existing files have been closed
    g_logfile.close();
    g_ide_cdrom.set_image(nullptr);
    g_ide_zipdrive.set_image(nullptr);
    g_ide_removable.set_image(nullptr);

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
/* Main IDE handling loop        */
/*********************************/

void load_image()
{
    // Clear any previous state
    g_ide_cdrom.set_image(nullptr);
    g_ide_zipdrive.set_image(nullptr);
    g_ide_removable.set_image(nullptr);
    g_ide_imagefile = IDEImageFile((uint8_t*)g_ide_buffer, sizeof(g_ide_buffer));
    
    bool found_image = false;

    char device_name[33] = {0};
    ini_gets("IDE", "device", "", device_name, sizeof(device_name), CONFIGFILE);
    drive_type_t type = DRIVE_TYPE_VIA_PREFIX;
    if (strcasecmp(device_name, "cdrom") == 0)
    {
        type  = DRIVE_TYPE_CDROM;
    }
    else if (strcasecmp(device_name, "zip100") == 0)
    {
        type = DRIVE_TYPE_ZIP100;
    }
    else if (strcasecmp(device_name, "removable") == 0)
    {
        type = DRIVE_TYPE_REMOVABLE;
    }
    else if (device_name[0])
    {
        logmsg("Warning device = [name] invalid, defaulting to CDROM");
        type = DRIVE_TYPE_CDROM;
    }
    else
    {
        type = (drive_type_t)ini_getl("IDE", "type", DRIVE_TYPE_VIA_PREFIX, CONFIGFILE);
        if (type != DRIVE_TYPE_CDROM && type != DRIVE_TYPE_ZIP100 && type != DRIVE_TYPE_VIA_PREFIX)
        {
            logmsg("Warning type = ", (int) type, " is invalid, setting is also depreciated. Use device = [name]");
            logmsg("Defaulting to device type using filename prefix");
            type = DRIVE_TYPE_VIA_PREFIX;
        }
    }
    g_ide_imagefile.set_drive_type(type);
    
    // Find image file
    char imagefile[MAX_FILE_PATH];
    if (g_ide_imagefile.find_next_image("/", NULL, imagefile, sizeof(imagefile)))
    {
        found_image = true;
    }

    switch (g_ide_imagefile.get_drive_type())
    {
    case DRIVE_TYPE_CDROM:
        g_ide_device = &g_ide_cdrom;
        logmsg("Device is a CDROM drive");
        break;
    case DRIVE_TYPE_ZIP100:
        g_ide_device = &g_ide_zipdrive;
        logmsg("Device is a Iomega Zip Drive 100");
        break;
    case DRIVE_TYPE_REMOVABLE:
        g_ide_device = &g_ide_removable;
        logmsg("Device is a generic removable drive");
        break;
    default:
        g_ide_device = &g_ide_cdrom;
        g_ide_imagefile.set_drive_type(DRIVE_TYPE_CDROM);
        logmsg("Device defaulting to a CDROM drive");
        break;
    }

    if (found_image)
    {
        logmsg("Loading image ", imagefile);
        g_ide_imagefile.open_file(SD.vol(), imagefile, false);
        if (g_ide_device) g_ide_device->set_image(&g_ide_imagefile);
        blinkStatus(BLINK_STATUS_OK);
    }
    else
    {
        
        logmsg("No image files found");
        blinkStatus(BLINK_ERROR_NO_IMAGES);
    }

}

void zuluide_setup(void)
{
    platform_init();
    platform_late_init();
    g_sdcard_present = mountSDCard();
    bool sd_card_initialized = false;
    if(!g_sdcard_present)
    {
        logmsg("SD card init failed, sdErrorCode: ", (int)SD.sdErrorCode(),
                    " sdErrorData: ", (int)SD.sdErrorData());
        logmsg("No SD card detected, defaulting to CD-ROM");
        blinkStatus(BLINK_ERROR_NO_SD_CARD);
    }
    else
    {
        if (SD.clusterCount() == 0)
        {
            logmsg("SD card without filesystem!");
        }

        print_sd_info();

        if (g_sdcard_present)
        {
            init_logfile();
            
            if (ini_getbool("IDE", "DisableStatusLED", false, CONFIGFILE))
            {
                platform_disable_led();
            }
            blinkStatus(BLINK_STATUS_OK);
        }

    }
    load_image();
    
    if (platform_get_device_id() == 1)
        ide_protocol_init(NULL, g_ide_device); // Secondary device
    else
        ide_protocol_init(g_ide_device, NULL); // Primary device
    logmsg("Initialization complete!");
}

void zuluide_main_loop(void)
{
    static uint32_t sd_card_check_time;
    static bool first_loop = true;

    if (first_loop)
    {
        // Give time for basic initialization to run
        // before checking SD card
        sd_card_check_time = millis() + 1000;
        first_loop = false;
    }

    platform_reset_watchdog();
    platform_poll();
    blink_poll();

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

                    g_ide_device->set_image(NULL);
                    g_ide_imagefile.close();
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
