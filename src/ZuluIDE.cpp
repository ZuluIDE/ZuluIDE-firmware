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
 * Under Section 7 of GPL version 3, you are granted additional
 * permissions described in the ZuluIDE Hardware Support Library Exception
 * (GPL-3.0_HSL_Exception.md), as published by Rabbit Hole Computing™.
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
#include "ide_rigid.h"
#include "ide_imagefile.h"
#include "status/status_controller.h"
#include <zuluide/status/cdrom_status.h>
#include <zuluide/status/removable_status.h>
#include <zuluide/status/rigid_status.h>
#include <zuluide/status/zip_status.h>
#include <zuluide/status/device_status.h>
#include <zuluide/status/system_status.h>
#include <zuluide/images/image_iterator.h>
#include <zuluide/pipe/image_request_pipe.h>
#include <zuluide/pipe/image_response_pipe.h>
#include "control/std_display_controller.h"
#include "control/control_interface.h"
#include "ZuluIDE_create_image.h"

#include <zip_parser.h>
bool g_sdcard_present;
extern SdFs SD;
static FsFile g_logfile;

static uint32_t g_ide_buffer[IDE_BUFFER_SIZE / 4];

// Currently supports one IDE device
static IDECDROMDevice g_ide_cdrom;
static IDEZipDrive g_ide_zipdrive;
static IDERemovable g_ide_removable;
static IDERigidDevice g_ide_rigid;
IDEImageFile g_ide_imagefile;
static IDEDevice *g_ide_device;
static bool g_loadedFirstImage = false;

zuluide::status::StatusController g_StatusController;
zuluide::pipe::ImageResponsePipe<zuluide::control::select_controller_source_t> g_ControllerImageResponsePipe;
zuluide::pipe::ImageRequestPipe<zuluide::control::select_controller_source_t> g_ControllerImageRequestPipe;
zuluide::control::StdDisplayController g_DisplayController(&g_StatusController, &g_ControllerImageRequestPipe, &g_ControllerImageResponsePipe);
zuluide::control::ControlInterface g_ControlInterface;
zuluide::status::SystemStatus g_previous_controller_status;

void status_observer(const zuluide::status::SystemStatus& current);
void loadFirstImage();
void load_image(const zuluide::images::Image& toLoad, bool insert = true);

static zuluide::ObserverTransfer<zuluide::status::SystemStatus> uiSafeStatusUpdater;

enum sniffer_mode_t {
  SNIFFER_OFF = 0,
  SNIFFER_ACTIVE = 1,
  SNIFFER_PASSIVE = 2
};
static sniffer_mode_t g_sniffer_mode;

/************************************/
/* Status reporting by blinking led */
/************************************/

#define BLINK_STATUS_OK 1
#define BLINK_DEFFERED_LOADING 2
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
    g_ide_rigid.set_image(nullptr);

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

// Checks if SD card is still present
bool poll_sd_card()
{
#ifdef SD_USE_SDIO
  return SD.card()->status() != 0 && SD.card()->errorCode() == 0;
#else
  uint32_t ocr;
  return SD.card()->readOCR(&ocr);
#endif
}

void print_sd_info()
{
    uint64_t size = (uint64_t)SD.vol()->clusterCount() * SD.vol()->bytesPerCluster();
    logmsg("SD card detected, FAT", (int)SD.vol()->fatType(),
                    " volume size: ", (int)(size / 1024 / 1024), " MB");

#if defined(HAS_SDIO_CLASS) && HAS_SDIO_CLASS
    int speed = ((SdioCard*)SD.card())->kHzSdClk();
    if (speed > 0)
    {
      logmsg("SD card communication speed: ",
        (int)((speed + 500) / 1000), " MHz, ",
        (int)((speed + 1000) / 2000), " MB/s");
    }
#endif

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

/*****************************/
/* Firmware update from .zip */
/*****************************/

// Check for firmware files meant for a different platform
static void check_for_unused_update_files()
{
  FsFile root = SD.open("/");
  FsFile file;
  char filename[MAX_FILE_PATH + 1];
  bool bin_files_found = false;
  while (file.openNext(&root, O_RDONLY))
  {
    if (!file.isDir())
    {
      size_t filename_len = file.getName(filename, sizeof(filename));
      if (strncasecmp(filename, "ZuluIDE", sizeof("ZuluIDE") - 1) == 0 &&
          strncasecmp(filename + filename_len - 4, ".bin", 4) == 0)
      {
        if (strncasecmp(filename, FIRMWARE_NAME_PREFIX, sizeof(FIRMWARE_NAME_PREFIX) - 1) == 0)
        {
          if (file.isReadOnly())
          {
              logmsg("The firmware file ", filename, " is read-only, the ZuluIDE will continue to update every power cycle with this SD card inserted");
          }
          else
          {
              logmsg("Found firmware file ", filename, " on the SD card, to update this ZuluIDE with the file please power cycle the board");
          }
        }
        else
        {
          bin_files_found = true;
          logmsg("Firmware update file \"", filename, "\" does not contain the board model string \"", FIRMWARE_NAME_PREFIX, "\"");
        }
      }
    }
  }
  if (bin_files_found)
  {
    logmsg("Please use the ", FIRMWARE_PREFIX ,"*.zip firmware bundle, or the proper .bin or .uf2 file to update the firmware.");
    logmsg("See ZuluIDE manual for more information");
  }
}

// When given a .zip file for firmware update, extract the file
// that matches this platform.
static void firmware_update()
{
  const char firmware_prefix[] = FIRMWARE_PREFIX;
  FsFile root = SD.open("/");
  FsFile file;
  char name[MAX_FILE_PATH + 1];
  while (1)
  {
    if (!file.openNext(&root, O_RDONLY))
    {
      file.close();
      root.close();
      return;
    }
    if (file.isDir())
      continue;

    file.getName(name, sizeof(name));
    if (strlen(name) + 1 < sizeof(firmware_prefix))
      continue;
    if ( strncasecmp(firmware_prefix, name, sizeof(firmware_prefix) -1) == 0)
    {
      break;
    }
  }

  logmsg("Found firmware package ", name);

  const uint32_t target_filename_length = sizeof(FIRMWARE_NAME_PREFIX "_2025-02-21_e4be9ed.bin") - 1;
  zipparser::Parser parser = zipparser::Parser(FIRMWARE_NAME_PREFIX, sizeof(FIRMWARE_NAME_PREFIX) - 1, target_filename_length);
  uint8_t buf[512];
  int32_t parsed_length;
  int bytes_read = 0;
  while ((bytes_read = file.read(buf, sizeof(buf))) > 0)
  {
    parsed_length = parser.Parse(buf, bytes_read);
    if (parsed_length == sizeof(buf))
       continue;
    if (parsed_length >= 0)
    {
      if (!parser.FoundMatch())
      {
        parser.Reset();
        file.seekSet(file.position() - (sizeof(buf) - parsed_length) + parser.GetCompressedSize());
      }
      else
      {
        // seek to start of compressed data in matching file
        file.seekSet(file.position() - (sizeof(buf) - parsed_length));
        break;
      }
    }
    if (parsed_length < 0)
    {
      logmsg("Filename character length of ", (int)target_filename_length , " with a prefix of ", FIRMWARE_NAME_PREFIX, " not found in ", name);
      file.close();
      root.close();
      return;
    }
  }


  if (parser.FoundMatch())
  {
    logmsg("Unzipping matching firmware with prefix: ", FIRMWARE_NAME_PREFIX);
    FsFile target_firmware;
    target_firmware.open(&root, FIRMWARE_NAME_PREFIX ".bin", O_BINARY | O_WRONLY | O_CREAT | O_TRUNC);
    uint32_t position = 0;
    while ((bytes_read = file.read(buf, sizeof(buf))) > 0)
    {
      if (bytes_read > parser.GetCompressedSize() - position)
        bytes_read =  parser.GetCompressedSize() - position;
      target_firmware.write(buf, bytes_read);
      position += bytes_read;
      if (position >= parser.GetCompressedSize())
      {
        break;
      }
    }
    // zip file has a central directory at the end of the file,
    // so the compressed data should never hit the end of the file
    // so bytes read should always be greater than 0 for a valid datastream
    if (bytes_read > 0)
    {
      target_firmware.close();
      file.close();
      root.remove(name);
      root.close();
      logmsg("Update extracted from package, rebooting MCU");
      platform_reset_mcu();
    }
    else
    {
      target_firmware.close();
      logmsg("Error reading firmware package file");
      root.remove(FIRMWARE_NAME_PREFIX ".bin");
    }
  }
  file.close();
  root.close();
}

/**************/
/* Log saving */
/**************/

void save_logfile(bool always = false)
{
    if(!mutex_try_enter(platform_get_log_mutex(), 0)) {
      return;
    }

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

    mutex_exit(platform_get_log_mutex());
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

drive_type_t searchForDriveType() {
  zuluide::images::ImageIterator imgIter;
  imgIter.Reset();
  while (imgIter.MoveNext()) {
    Image image = imgIter.Get();

    if (image.GetImageType() == Image::ImageType::cdrom) {
      return DRIVE_TYPE_CDROM;
    }

    auto imageType = Image::InferImageTypeFromFileName(image.GetFilename().c_str());
    if (imageType != Image::ImageType::unknown) {
      imgIter.Cleanup();
      g_ide_imagefile.set_prefix(Image::GetImagePrefix(imageType));
      return Image::ToDriveType(imageType);
    }
  }

  imgIter.Cleanup();

  // If nothing is found, default to a CDROM.
  return DRIVE_TYPE_CDROM;
}

/***
 * Configures the status controller. The status controller is used to
*/
void setupStatusController()
{
  g_ControllerImageRequestPipe.Reset();
  g_ControllerImageResponsePipe.Reset();
  g_ControllerImageRequestPipe.AddObserver(
    [](zuluide::pipe::ImageRequest<zuluide::control::select_controller_source_t> t)
    {
      g_ControllerImageResponsePipe.HandleRequest(t);
    }
  );
  platform_set_controller_image_response_pipe(&g_ControllerImageResponsePipe);
  g_StatusController.Reset();
  g_StatusController.SetFirmwareVersion(std::string(g_log_firmwareversion));
  bool isPrimary = platform_get_device_id() == 0;
  char device_name[33] = {0};

  ini_gets("IDE", "device", "", device_name, sizeof(device_name), CONFIGFILE);
  std::unique_ptr<zuluide::status::IDeviceStatus> device;
  if (!g_sdcard_present) {
    logmsg("SD card not loaded, defaulting to CD-ROM");
    g_ide_imagefile.set_drive_type(drive_type_t::DRIVE_TYPE_CDROM);
  } else if (strncasecmp(device_name, "cdrom", sizeof("cdrom")) == 0) {
    g_ide_imagefile.set_drive_type(drive_type_t::DRIVE_TYPE_CDROM);
  } else if (strncasecmp(device_name, "zip100", sizeof("zip100")) == 0) {
    g_ide_imagefile.set_drive_type(drive_type_t::DRIVE_TYPE_ZIP100);
  } else if (strncasecmp(device_name, "zip250", sizeof("zip250")) == 0) {
    g_ide_imagefile.set_drive_type(drive_type_t::DRIVE_TYPE_ZIP250);
  } else if (strncasecmp(device_name, "removable", sizeof("removable")) == 0) {
    g_ide_imagefile.set_drive_type(drive_type_t::DRIVE_TYPE_REMOVABLE);
  } else if (strncasecmp(device_name, "hdd", sizeof("hdd")) == 0) {
    g_ide_imagefile.set_drive_type(drive_type_t::DRIVE_TYPE_RIGID);
  } else if (device_name[0] && g_sdcard_present) {
    logmsg("Warning device = \"", device_name ,"\" invalid, defaulting to CD-ROM");
    g_ide_imagefile.set_drive_type(drive_type_t::DRIVE_TYPE_CDROM);
  } else {
    logmsg("Selecting device type when loading first image.");
  }
    // If the device type is not setup.
  if (g_ide_imagefile.get_drive_type() == drive_type_t::DRIVE_TYPE_VIA_PREFIX) {
    drive_type_t newDriveType = searchForDriveType();
    g_ide_imagefile.set_drive_type(newDriveType);
  }

  switch (g_ide_imagefile.get_drive_type())
  {
  case DRIVE_TYPE_CDROM:
    g_ide_device = &g_ide_cdrom;
    device = std::move(std::make_unique<zuluide::status::CDROMStatus>(zuluide::status::CDROMStatus::Status::NoImage, zuluide::status::CDROMStatus::DriveSpeed::Single));
    logmsg("Device is a CDROM drive");
    break;
  case DRIVE_TYPE_ZIP100:
    g_ide_device = &g_ide_zipdrive;
    device = std::move(std::make_unique<zuluide::status::ZipStatus>(zuluide::status::ZipStatus::Status::NoImage, zuluide::status::ZipStatus::ZipDriveType::Zip100));
    logmsg("Device is a Iomega Zip Drive 100");
    break;
  case DRIVE_TYPE_ZIP250:
    g_ide_device = &g_ide_zipdrive;
    device = std::move(std::make_unique<zuluide::status::ZipStatus>(zuluide::status::ZipStatus::Status::NoImage, zuluide::status::ZipStatus::ZipDriveType::Zip250));
    logmsg("Device is a Iomega Zip Drive 250");
    break;
  case DRIVE_TYPE_REMOVABLE:
    g_ide_device = &g_ide_removable;
    device = std::move(std::make_unique<zuluide::status::RemovableStatus>(zuluide::status::RemovableStatus::Status::NoImage));
    logmsg("Device is a generic removable drive");
    break;
  case DRIVE_TYPE_RIGID:
    g_ide_device = &g_ide_rigid;
    device = std::move(std::make_unique<zuluide::status::RigidStatus>(zuluide::status::RigidStatus::Status::NoImage));
    logmsg("Device is a hard drive");
    break;
  default:
    g_ide_device = &g_ide_cdrom;
    g_ide_imagefile.set_drive_type(DRIVE_TYPE_CDROM);
    device = std::move(std::make_unique<zuluide::status::CDROMStatus>(zuluide::status::CDROMStatus::Status::NoImage, zuluide::status::CDROMStatus::DriveSpeed::Single));
    logmsg("Device defaulting to a CDROM drive");
    break;
  }

  g_StatusController.SetIsPreventRemovable(false);
  g_StatusController.SetIsDeferred(false);

  if (isPrimary)
      ide_protocol_init(g_ide_device, NULL); // Primary device
  else
      ide_protocol_init(NULL, g_ide_device); // Secondary device



  if (device) {
    g_StatusController.SetIsPrimary(isPrimary);
    g_StatusController.UpdateDeviceStatus(std::move(device));
  }

  g_StatusController.AddObserver(status_observer);

  if (platform_check_for_controller())
  {
    platform_set_device_control(&g_StatusController);
    platform_set_status_controller(&uiSafeStatusUpdater);
    platform_set_display_controller(g_DisplayController);

    g_ControlInterface.SetDisplayController(&g_DisplayController);

    platform_set_input_interface(&g_ControlInterface);

    // Propogate updates to the control interface from the UI core.
    uiSafeStatusUpdater.AddObserver([](zuluide::status::SystemStatus t) { g_DisplayController.ProcessSystemStatusUpdate(t); });
    uiSafeStatusUpdater.AddObserver([](zuluide::status::SystemStatus t) { g_ControlInterface.HandleSystemStatusUpdate(t); });

    g_DisplayController.SetMode(zuluide::control::Mode::Splash);

    // Force an update.
    g_StatusController.EndUpdate();

    // This enables system updates to start flowing to the UI from this point forward.
    uiSafeStatusUpdater.Initialize(g_StatusController, true);
  }
  else
  {
    g_StatusController.EndUpdate();
  }

  if (isPrimary)
    ide_protocol_init(g_ide_device, NULL); // Primary device
  else
    ide_protocol_init(NULL, g_ide_device); // Secondary device
  if (g_ide_device->is_removable() && ini_getbool("IDE", "no_media_on_init", 0, CONFIGFILE))
  {
    g_ide_device->set_image(nullptr);
    g_ide_device->set_loaded_without_media(true);
    g_ide_device->set_load_first_image_cb(loadFirstImage);
  }
  else
  {
        loadFirstImage();
  }
}

void loadFirstImage() {
  bool quiet = ini_getbool("IDE", "quiet_image_parsing", 0, CONFIGFILE);
  if (!quiet) logmsg("Parsing images on the SD card");
  zuluide::images::ImageIterator imgIterator;
  bool success = false;
  if (ini_getbool("IDE", "init_with_last_used_image", 1, CONFIGFILE))
  {
    imgIterator.Reset(!quiet);
    FsFile last_saved = SD.open(LASTFILE, O_RDONLY);
    if (last_saved.isOpen())
    {
      String image_name = last_saved.readStringUntil('\n');
      last_saved.close();
      if (imgIterator.MoveToFile(image_name.c_str()))
      {
        if (!quiet) logmsg("-- Loading last used image: \"", image_name.c_str(), "\"");
        g_StatusController.LoadImage(imgIterator.Get());
        g_previous_controller_status = g_StatusController.GetStatus();
        g_loadedFirstImage = true;
        load_image(imgIterator.Get(), false);
        success = true;
      }
      if (!success)
      {
        if (!quiet) logmsg("-- Last used image \"", image_name.c_str(), "\" not found");
      }
    }
    quiet = true;
  }

  if (!success)
  {

    imgIterator.Reset(!quiet);
    if (!imgIterator.IsEmpty() && imgIterator.MoveNext()) {
      logmsg("Loading first image ", imgIterator.Get().GetFilename().c_str());
      g_StatusController.LoadImage(imgIterator.Get());
      g_previous_controller_status = g_StatusController.GetStatus();
      g_loadedFirstImage = true;
      load_image(imgIterator.Get(), false);
    } else {
      logmsg("No valid image files found");
      blinkStatus(BLINK_ERROR_NO_IMAGES);
    }
  }

  if (g_loadedFirstImage)
     g_ide_device->post_image_setup();

  imgIterator.Cleanup();
}


/*********************************/
/* Main IDE handling loop        */
/*********************************/

void clear_image() {
  // Clear any previous state
  g_ide_cdrom.set_image(nullptr);
  g_ide_zipdrive.set_image(nullptr);
  g_ide_removable.set_image(nullptr);
  g_ide_rigid.set_image(nullptr);
  g_ide_imagefile.clear();

  // Set the drive type for the image from the system state.
  if (g_ide_imagefile.get_drive_type() != drive_type_t::DRIVE_TYPE_VIA_PREFIX) {
    g_ide_imagefile.set_drive_type(g_StatusController.GetStatus().GetDeviceType());
  }
}

void status_observer(const zuluide::status::SystemStatus& current) {
  // We need to check and see what changes have occurred.
  if (g_ide_device->is_loaded_without_media() && current.HasLoadedImage()) {

    load_image(current.GetLoadedImage());
    g_ide_device->set_loaded_without_media(false);
    g_loadedFirstImage = true;
    g_ide_device->loaded_new_media();
  }
  else if ((g_loadedFirstImage && !current.LoadedImagesAreEqual(g_previous_controller_status))) {
    // The current image has changed.
    if (current.HasLoadedImage())
    {
      load_image(current.GetLoadedImage());
      g_ide_device->loaded_new_media();
    } 
    else
    {
      if (!g_ide_device->is_load_deferred())
        g_ide_device->set_loaded_without_media(true);
    }
  }
  g_previous_controller_status = current;
}

void load_image(const zuluide::images::Image& toLoad, bool insert)
{

  if (g_loadedFirstImage && g_ide_device->set_load_deferred(toLoad.GetFilename().c_str()))
    return blinkStatus(BLINK_DEFFERED_LOADING);
  
  clear_image();
   
  logmsg("Loading image \"", toLoad.GetFilename().c_str(), "\"");
  g_ide_imagefile.open_file(toLoad.GetFilename().c_str(), false);
  if (g_ide_device) {
    if (insert)
      g_ide_device->insert_media(&g_ide_imagefile);
    else
      g_ide_device->set_image(&g_ide_imagefile);
  }

  if (ini_getbool("IDE", "init_with_last_used_image", 1, CONFIGFILE))
  {
      FsFile last_file = SD.open(LASTFILE, O_WRONLY | O_CREAT | O_TRUNC);
      if (last_file.isOpen())
        last_file.write(toLoad.GetFilename().c_str());
      last_file.close();
  }

  blinkStatus(BLINK_STATUS_OK);
}


static void zuluide_reload_config()
{
  if (ini_haskey("IDE", "debug", CONFIGFILE))
  {
    g_log_debug = ini_getbool("IDE", "debug", g_log_debug, CONFIGFILE);
    logmsg("-- Debug log setting overridden in " CONFIGFILE ", debug = ", (int)g_log_debug);
  }

  g_sniffer_mode = (sniffer_mode_t)ini_getl("IDE", "sniffer", 0, CONFIGFILE);

  if (g_sniffer_mode != SNIFFER_OFF)
  {
#ifdef PLATFORM_HAS_SNIFFER
    SD.remove("sniff.dat");
    if (platform_enable_sniffer("sniff.dat", g_sniffer_mode == SNIFFER_PASSIVE))
    {
      logmsg("-- Storing IDE bus traffic to sniff.dat");
      if (g_sniffer_mode == SNIFFER_PASSIVE) logmsg("-- Normal IDE bus operation is disabled by passive sniffer mode");
    }
    else
    {
      logmsg("-- Failed to initialize IDE bus sniffer");
      g_sniffer_mode = SNIFFER_OFF;
    }
#else
    logmsg("-- This platform does not support IDE bus sniffer");
    g_sniffer_mode = SNIFFER_OFF;
#endif
  }

  if (ini_getbool("IDE", "DisableStatusLED", false, CONFIGFILE))
  {
      platform_disable_led();
  }

  uint8_t eject_button = ini_getl("IDE", "eject_button", 1, CONFIGFILE);
  platform_init_eject_button(eject_button);
}

static void zuluide_setup_sd_card()
{
    g_sdcard_present = mountSDCard();
    if(!g_sdcard_present)
    {
        g_StatusController.SetIsCardPresent(false);
        blinkStatus(BLINK_ERROR_NO_SD_CARD);
    }
    else
    {
        g_StatusController.SetIsCardPresent(true);
        if (SD.clusterCount() == 0)
        {
            logmsg("SD card without filesystem!");
        }

        print_sd_info();

        if (g_sdcard_present)
        {
            init_logfile();
        }

        check_for_unused_update_files();
        firmware_update();
        searchAndCreateImage((uint8_t*) g_ide_buffer, sizeof(g_ide_buffer));
    }
}


void zuluide_init(void)
{
  platform_init();
  platform_late_init();
  zuluide_setup_sd_card();
  zuluide_reload_config();

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

  g_ide_imagefile = IDEImageFile((uint8_t*)g_ide_buffer, sizeof(g_ide_buffer));

  // Setup the status controller.
  setupStatusController();

  if (!g_ide_device->is_medium_present())
  {
    // Set to ejected state if there is no media present
    g_ide_device->eject_media();
  }

  blinkStatus(BLINK_STATUS_OK);
  logmsg("Initialization complete!");
}

void zuluide_main_loop(void)
{
    static uint32_t sd_card_check_time;
    static uint32_t splash_check_time;
    static bool splash_over = false;
    static bool first_loop = true;

    if (first_loop)
    {
        // Give time for basic initialization to run
        // before checking SD card
        sd_card_check_time = millis() + 1000;
        splash_check_time = millis();
        first_loop = false;
    }
    platform_reset_watchdog();
    platform_poll(true);
    g_ide_device->eject_button_poll(true);
    blink_poll();

    g_StatusController.ProcessUpdates();
    g_ControllerImageRequestPipe.ProcessUpdates();
    
    // Checks after 3 seconds if we are still on the Splash screen ( for example if there is no SD card)
    if (!splash_over && (uint32_t)(millis() - splash_check_time) > 3000)
    {
      if (g_DisplayController.GetMode() == zuluide::control::Mode::Splash)
      {
        // Need to force a status controller update to move beyond the Splash screen
        g_StatusController.SetFirmwareVersion(std::string(g_log_firmwareversion));
      }
      splash_over = true;
    }

    save_logfile();

    if (g_sniffer_mode != SNIFFER_PASSIVE)
    {
      ide_protocol_poll();
    }

#ifdef PLATFORM_HAS_SNIFFER
    if (g_sniffer_mode != SNIFFER_OFF)
    {
      platform_sniffer_poll();
    }
#endif

    if (g_sdcard_present)
    {
        // Check SD card status for hotplug
        if ((uint32_t)(millis() - sd_card_check_time) > 5000)
        {
            sd_card_check_time = millis();
            if (!poll_sd_card())
            {
                if (!poll_sd_card())
                {
                    g_sdcard_present = false;
                    g_StatusController.SetIsCardPresent(false);
                    logmsg("SD card removed, trying to reinit");
                    if (g_ide_device->is_removable())
                    {
                        g_ide_device->eject_media();
                    }
                    g_ide_imagefile.close();
                    g_ide_device->set_image(nullptr);
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
            zuluide_reload_config();
            searchAndCreateImage((uint8_t*) g_ide_buffer, sizeof(g_ide_buffer));

            g_StatusController.SetIsCardPresent(true);
            if (g_ide_device->is_removable() && ini_getbool("IDE", "no_media_on_sd_insert", 0, CONFIGFILE))
            {
                g_ide_device->set_loaded_without_media(true);
                g_loadedFirstImage = false;
                g_ide_device->set_load_first_image_cb(loadFirstImage);
            }

            if (!g_ide_device->is_loaded_without_media())
            {
              loadFirstImage();
              g_ide_device->sd_card_inserted();
            }
        }
        else
        {
            blinkStatus(BLINK_ERROR_NO_SD_CARD);
        }

        sd_card_check_time = millis();
    }
}
