/* ZuluIDE
 *  Copyright (c) 2022 Rabbit Hole Computing
 */

#include <SdFat.h>
#include <minIni.h>
#include "ZuluIDE_config.h"
#include "ZuluIDE_platform.h"
#include "ZuluIDE_log.h"

SdFs SD;
FsFile g_logfile;
static bool g_sdcard_present;

/************************************/
/* Status reporting by blinking led */
/************************************/

#define BLINK_STATUS_OK 1
#define BLINK_ERROR_NO_IMAGES  3 
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

/**************/
/* Log saving */
/**************/

void save_logfile(bool always = false)
{
  static uint32_t prev_log_pos = 0;
  static uint32_t prev_log_len = 0;
  static uint32_t prev_log_save = 0;
  uint32_t loglen = azlog_get_buffer_len();

  if (loglen != prev_log_len && g_sdcard_present)
  {
    // When debug is off, save log at most every LOG_SAVE_INTERVAL_MS
    // When debug is on, save after every SCSI command.
    if (always || g_azlog_debug || (LOG_SAVE_INTERVAL_MS > 0 && (uint32_t)(millis() - prev_log_save) > LOG_SAVE_INTERVAL_MS))
    {
      g_logfile.write(azlog_get_buffer(&prev_log_pos));
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
    azlog("Failed to open log file: ", SD.sdErrorCode());
  }
  save_logfile(true);

  first_open_after_boot = false;
}

void print_sd_info()
{
  uint64_t size = (uint64_t)SD.vol()->clusterCount() * SD.vol()->bytesPerCluster();
  azlog("SD card detected, FAT", (int)SD.vol()->fatType(),
          " volume size: ", (int)(size / 1024 / 1024), " MB");
  
  cid_t sd_cid;

  if(SD.card()->readCID(&sd_cid))
  {
    azlog("SD MID: ", (uint8_t)sd_cid.mid, ", OID: ", (uint8_t)sd_cid.oid[0], " ", (uint8_t)sd_cid.oid[1]);
    
    char sdname[6] = {sd_cid.pnm[0], sd_cid.pnm[1], sd_cid.pnm[2], sd_cid.pnm[3], sd_cid.pnm[4], 0};
    azlog("SD Name: ", sdname);
    azlog("SD Date: ", (int)sd_cid.mdtMonth(), "/", sd_cid.mdtYear());
    azlog("SD Serial: ", sd_cid.psn());
  }
}

/*********************************/
/* Main IDE handling loop       */
/*********************************/

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

void ide_phy_test();

extern "C" void zuluide_setup(void)
{
  azplatform_init();
  azplatform_late_init();
  ide_phy_test();

  g_sdcard_present = mountSDCard();

  if(!g_sdcard_present)
  {
    azlog("SD card init failed, sdErrorCode: ", (int)SD.sdErrorCode(),
           " sdErrorData: ", (int)SD.sdErrorData());
    
    do
    {
      blinkStatus(BLINK_ERROR_NO_SD_CARD);
      delay(1000);
      azplatform_reset_watchdog();
      g_sdcard_present = mountSDCard();
    } while (!g_sdcard_present);
    azlog("SD card init succeeded after retry");
  }

  if (g_sdcard_present)
  {
    if (SD.clusterCount() == 0)
    {
      azlog("SD card without filesystem!");
    }

    print_sd_info();
  }

  azlog("Initialization complete!");
  blinkStatus(BLINK_STATUS_OK);

  if (g_sdcard_present)
  {
    init_logfile();
    if (ini_getbool("SCSI", "DisableStatusLED", false, CONFIGFILE))
    {
      azplatform_disable_led();
    }
  }
}

extern "C" void zuluide_main_loop(void)
{
  static uint32_t sd_card_check_time = 0;

  azplatform_reset_watchdog();

  save_logfile();

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
          azlog("SD card removed, trying to reinit");
        }
      }
    }
  }

  if (!g_sdcard_present)
  {
    // Try to remount SD card
    do 
    {
      g_sdcard_present = mountSDCard();

      if (g_sdcard_present)
      {
        azlog("SD card reinit succeeded");
        print_sd_info();

        init_logfile();
        blinkStatus(BLINK_STATUS_OK);
      }
      else
      {
        blinkStatus(BLINK_ERROR_NO_SD_CARD);
        delay(1000);
        azplatform_reset_watchdog();
      }
    } while (!g_sdcard_present);
  }
}
