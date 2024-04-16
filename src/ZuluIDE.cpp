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
#include "ZuluIDE_msc.h"
#include "ZuluIDE_log.h"
#include "ide_protocol.h"
#include "ide_cdrom.h"
#include "ide_zipdrive.h"
#include "ide_removable.h"
#include "ide_imagefile.h"
#include "status/status_controller.h"
#include <zuluide/status/cdrom_status.h>
#include <zuluide/status/removable_status.h>
#include <zuluide/status/zip100_status.h>
#include <zuluide/status/device_status.h>
#include <zuluide/status/system_status.h>
#include <zuluide/images/image_iterator.h>
#include "control/std_display_controller.h"
#include "control/control_interface.h"

bool g_sdcard_present;
static FsFile g_logfile;

static uint32_t g_ide_buffer[IDE_BUFFER_SIZE / 4];

// Currently supports one IDE device
static IDECDROMDevice g_ide_cdrom;
static IDEZipDrive g_ide_zipdrive;
static IDERemovable g_ide_removable;
static IDEImageFile g_ide_imagefile;
static IDEDevice *g_ide_device;

zuluide::status::StatusController g_StatusController;
zuluide::control::StdDisplayController g_DisplayController(&g_StatusController);
zuluide::control::ControlInterface g_ControlInterface;
zuluide::status::SystemStatus previous;
void status_observer(const zuluide::status::SystemStatus& current);
void loadFirstImage();


/************************************/
/* Status reporting by blinking led */
/************************************/

#define BLINK_STATUS_OK 1
#define BLINK_ERROR_NO_IMAGES    3
#define BLINK_ERROR_NO_SD_CARD 5

static uint16_t blink_count = 0;
static uint32_t blink_start = 0;
static uint32_t blink_delay = 0;
static uint32_t blink_end_delay= 0;

bool blink_poll()
{
    bool is_blinking = true;
    
    if (blink_count == 0)
    {
        is_blinking = false;
    }
    else if (blink_count == 1 && ((uint32_t)(millis() - blink_start)) > blink_end_delay )
    {
        LED_OFF_OVERRIDE();
        blink_count = 0;
        is_blinking = false;
    }
    else if (blink_count > 1 && ((uint32_t)(millis() - blink_start)) > blink_delay)
    {
        if (1 & blink_count)
            LED_ON_OVERRIDE();
        else
            LED_OFF_OVERRIDE();
        blink_count--;
        blink_start = millis();
    }

    if (!is_blinking)
        platform_set_blink_status(false);
    return is_blinking;
}

void blink_cancel()
{
    blink_count = 0;
}

void blinkStatus(uint8_t times, uint32_t delay = 500, uint32_t end_delay = 1250)
{
    if (!blink_poll() && blink_count == 0)
    {
        blink_start = millis();
        blink_count = 2 * times + 1;
        blink_delay = delay / 2;
        blink_end_delay =  end_delay;
        platform_set_blink_status(true);
        LED_OFF_OVERRIDE();
    }
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

/***
 * Configures the status controller. The status controller is used to
*/
void setupStatusController()
{
  g_StatusController.Reset();
  g_StatusController.SetFirmwareVersion(std::string(g_log_firmwareversion));
  bool isPrimary = platform_get_device_id() == 0;
  char device_name[33] = {0};

  ini_gets("IDE", "device", "", device_name, sizeof(device_name), CONFIGFILE);
  std::unique_ptr<zuluide::status::IDeviceStatus> device;
  if (strncasecmp(device_name, "cdrom", sizeof("cdrom")) == 0) {
    device = std::move(std::make_unique<zuluide::status::CDROMStatus>(zuluide::status::CDROMStatus::Status::NoImage, zuluide::status::CDROMStatus::DriveSpeed::Single));
  } else if (strncasecmp(device_name, "zip100", sizeof("zip100")) == 0) {
    device = std::move(std::make_unique<zuluide::status::Zip100Status>(zuluide::status::Zip100Status::Status::NoImage));
  } else if (strncasecmp(device_name, "removable", sizeof("removable")) == 0) {
    device = std::move(std::make_unique<zuluide::status::RemovableStatus>(zuluide::status::RemovableStatus::Status::NoImage));
  } else if (device_name[0]) {
    logmsg("Warning device = [name] invalid, defaulting to CDROM");
    device = std::move(std::make_unique<zuluide::status::CDROMStatus>(zuluide::status::CDROMStatus::Status::NoImage, zuluide::status::CDROMStatus::DriveSpeed::Single));
  } else {
    logmsg("Selecting device type when loading first image.");
  }

  if (device) {
    g_StatusController.SetIsPrimary(isPrimary);
    g_StatusController.UpdateDeviceStatus(std::move(device));
  }
  g_StatusController.AddObserver(status_observer);

  if (platform_check_for_controller())
  {
    platform_set_status_controller(g_StatusController);
    platform_set_display_controller(g_DisplayController);

    g_ControlInterface.SetDisplayController(&g_DisplayController);
    g_ControlInterface.SetStatusController(&g_StatusController);

    platform_set_input_interface(&g_ControlInterface);

    // Force an update.
    g_StatusController.EndUpdate();

    g_DisplayController.SetMode(zuluide::control::Mode::Status);
  }
  else
  {
    g_StatusController.EndUpdate();
  }
  loadFirstImage();  
}

void loadFirstImage() {
  zuluide::images::ImageIterator imgIterator;
  imgIterator.Reset();
  if (!imgIterator.IsEmpty() && imgIterator.MoveNext()) {
    logmsg("Loading first image ", imgIterator.Get().GetFilename().c_str());
    g_StatusController.LoadImage(imgIterator.Get());
  } else {
    logmsg("No image files found");
    blinkStatus(BLINK_ERROR_NO_IMAGES);
  }
}


/*********************************/
/* Main IDE handling loop        */
/*********************************/
drive_type_t searchForDriveType() {
  zuluide::images::ImageIterator imgIter;
  imgIter.Reset();
  while(imgIter.MoveNext()) {
    auto image = imgIter.Get().GetFilename().c_str();
    if (strncasecmp(image, "cdrm", sizeof("cdrm")) == 0) {
      return DRIVE_TYPE_CDROM;
    } else if (strncasecmp(image, "zipd", sizeof("zipd")) == 0) {
      return DRIVE_TYPE_ZIP100;
    } else if (strncasecmp(image, "remv", sizeof("remv")) == 0) {
      return DRIVE_TYPE_REMOVABLE;
    }
  }

  // If nothing is found, default to a CDROM.
  return drive_type_t::DRIVE_TYPE_CDROM;
}

void load_image(const zuluide::images::Image& toLoad);

void clear_image() {
  // Clear any previous state
  g_ide_cdrom.set_image(nullptr);
  g_ide_zipdrive.set_image(nullptr);
  g_ide_removable.set_image(nullptr);
  g_ide_imagefile = IDEImageFile((uint8_t*)g_ide_buffer, sizeof(g_ide_buffer));

  // Set the drive type for the image from the system state.
  if (g_ide_imagefile.get_drive_type() != drive_type_t::DRIVE_TYPE_VIA_PREFIX) {
    g_ide_imagefile.set_drive_type(g_StatusController.GetStatus().GetDeviceType());
  }
}

void status_observer(const zuluide::status::SystemStatus& current) {
  // We need to check and see what changes have occured.
  if (!current.LoadedImagesAreEqual(previous)) {
    // The current image has changed.
    if (current.HasLoadedImage()) {
      load_image(current.GetLoadedImage());
    } else {
      clear_image();
    }
  }

  previous = current;
}

void load_image(const zuluide::images::Image& toLoad)
{
  clear_image();

  // If the device type is not setup.
  if (g_ide_imagefile.get_drive_type() == drive_type_t::DRIVE_TYPE_VIA_PREFIX) {
    drive_type_t newDriveType = searchForDriveType();
    g_ide_imagefile.set_drive_type(newDriveType);
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

  logmsg("Loading image ", toLoad.GetFilename().c_str());
  g_ide_imagefile.open_file(toLoad.GetFilename().c_str(), false);
  if (g_ide_device) {
    g_ide_device->set_image(&g_ide_imagefile);
  }
  
  blinkStatus(BLINK_STATUS_OK);
}

static void zuluide_setup_sd_card()
{
    g_sdcard_present = mountSDCard();
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
        }
    }
}

void zuluide_init(void)
{
    platform_init();
    platform_late_init();
    zuluide_setup_sd_card();

#ifdef PLATFORM_MASS_STORAGE
  static bool check_mass_storage = true;
  if (check_mass_storage && ini_getbool("IDE", "enable_usb_mass_storage", false, CONFIGFILE))
  {
    check_mass_storage = false;

    // perform checks to see if a computer is attached and return true if we should enter MSC mode.
    if (platform_sense_msc())
    {
      zuluide_msc_loop();
      logmsg("Re-processing filenames and zuluide.ini config parameters");
      zuluide_setup_sd_card();
    }
  }
#endif
    // Setup the status controller.
    setupStatusController();

    if (platform_get_device_id() == 1)
        ide_protocol_init(NULL, g_ide_device); // Secondary device
    else
        ide_protocol_init(g_ide_device, NULL); // Primary device

    blinkStatus(BLINK_STATUS_OK);


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

            g_DisplayController.SetMode(zuluide::control::Mode::Select);
            loadFirstImage();
        }
        else
        {
            blinkStatus(BLINK_ERROR_NO_SD_CARD);
        }

        sd_card_check_time = millis();
    }
}