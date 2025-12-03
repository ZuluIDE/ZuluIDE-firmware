/**
 * Copyright (c) 2023-2024 zigzagjoe
 * 
 * ZuluSCSI™ firmware is licensed under the GPL version 3 or any later version. 
 * Changed for use with ZuluIDE firmware.
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

/* platform specific MSC routines */
#ifdef PLATFORM_MASS_STORAGE

#include <SdFat.h>
#include <device/usbd.h>
#include <hardware/gpio.h>
#include "ZuluIDE_platform.h"
#include "ZuluIDE_log.h"
#include "ZuluIDE_msc.h"
#include <USB.h>
#include <class/msc/msc.h>
#include <class/msc/msc_device.h>

#if CFG_TUD_MSC_EP_BUFSIZE < SD_SECTOR_SIZE
  #error "CFG_TUD_MSC_EP_BUFSIZE is too small! It needs to be at least 512 (SD_SECTOR_SIZE)"
#endif

// external global SD variable
extern SdFs SD;

static struct {
  bool    unitReady = false;
  uint8_t usbEpOut;
  uint8_t usbEpIn;
  uint8_t usbId;
  bool usbRegistered = false;

} g_MSC;


/* return true if USB presence detected / eligble to enter CR mode */
bool platform_sense_msc() {

  logmsg("Waiting for USB enumeration to enter Card Reader mode.");

  // wait for up to a second to be enumerated
  uint32_t start = millis();
  while (!tud_connected() && ((uint32_t)(millis() - start) < CR_ENUM_TIMEOUT)) 
    delay(100);

  // tud_connected returns True if just got out of Bus Reset and received the very first data from host
  // https://github.com/hathach/tinyusb/blob/master/src/device/usbd.h#L63
  return tud_connected();
}

/* return true if we should remain in card reader mode and perform periodic tasks */
bool platform_run_msc() {
  return g_MSC.unitReady;
}

/* perform MSC class preinit tasks */
void platform_enter_msc() {
  dbgmsg("USB MSC buffer size: ", CFG_TUD_MSC_EP_BUFSIZE);
  // MSC is ready for read/write
  // we don't need any prep, but the var is required as the MSC callbacks are always active
  if (!g_MSC.usbRegistered) {
    USB.disconnect();
    g_MSC.usbEpIn = USB.registerEndpointIn();
    g_MSC.usbEpOut = USB.registerEndpointOut();
    static uint8_t msd_desc[] = { TUD_MSC_DESCRIPTOR(1 /* placeholder */, 0, g_MSC.usbEpOut, g_MSC.usbEpIn, USBD_MSC_EPSIZE) };
    g_MSC.usbId = USB.registerInterface(1, USBClass::simpleInterface, msd_desc, sizeof(msd_desc), 2, 0);
    g_MSC.unitReady = true;
    USB.connect();
    g_MSC.usbRegistered = true;
  }
}

/* perform any cleanup tasks for the MSC-specific functionality */
void platform_exit_msc() {
  g_MSC.unitReady = false;
  if (g_MSC.usbRegistered)
  {
    USB.disconnect();
    USB.unregisterInterface(g_MSC.usbId);
    USB.unregisterEndpointOut(g_MSC.usbEpOut);
    USB.unregisterEndpointIn(g_MSC.usbEpIn);
    USB.connect();
    g_MSC.usbRegistered = false;
  }
}

/* TinyUSB mass storage callbacks follow */

// Invoked when received SCSI_CMD_INQUIRY
// fill vendor id, product id and revision with string up to 8, 16, 4 characters respectively
extern "C" void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8],
                        uint8_t product_id[16], uint8_t product_rev[4]) {

  // TODO: We could/should use strings from the platform, but they are too long
  const char vid[] = "RHC";
  const char pid[] = "ZuluIDE"; 
  const char rev[] = "1.0";

  memcpy(vendor_id, vid, tu_min32(strlen(vid), 8));
  memcpy(product_id, pid, tu_min32(strlen(pid), 16));
  memcpy(product_rev, rev, tu_min32(strlen(rev), 4));
}

// max LUN supported
// we only have the one SD card
extern "C" uint8_t tud_msc_get_maxlun_cb(void) {
  return 1; // number of LUNs supported
}

// return writable status
// on platform supporting write protect switch, could do that here.
// otherwise this is not actually needed
extern "C" bool tud_msc_is_writable_cb (uint8_t lun)
{
  (void) lun;
  return g_MSC.unitReady;
}

// see https://www.seagate.com/files/staticfiles/support/docs/manual/Interface%20manuals/100293068j.pdf pg 221
extern "C" bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject)
{
  (void) lun;
  (void) power_condition;

  if (load_eject)  {
    if (start) {
      // load disk storage
      // do nothing as we started "loaded"
    } else {
      g_MSC.unitReady = false;
    }
  }

  return true;
}

// return true if we are ready to service reads/writes
extern "C" bool tud_msc_test_unit_ready_cb(uint8_t lun) {
  (void) lun;

  return g_MSC.unitReady;
}

// return size in blocks and block size
extern "C" void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count,
                         uint16_t *block_size) {
  (void) lun;

  *block_count = g_MSC.unitReady ? (SD.card()->sectorCount()) : 0;
  *block_size = SD_SECTOR_SIZE;
}

// Callback invoked when received an SCSI command not in built-in list (below) which have their own callbacks
// - READ_CAPACITY10, READ_FORMAT_CAPACITY, INQUIRY, MODE_SENSE6, REQUEST_SENSE, READ10 and WRITE10
extern "C" int32_t tud_msc_scsi_cb(uint8_t lun, const uint8_t scsi_cmd[16], void *buffer,
                        uint16_t bufsize) {

  const void *response = NULL;
  uint16_t resplen = 0;

  switch (scsi_cmd[0]) {
  case SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL:
    // Host is about to read/write etc ... better not to disconnect disk
    resplen = 0;
    break;

  default:
    // Set Sense = Invalid Command Operation
    tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);

    // negative means error -> tinyusb could stall and/or response with failed status
    resplen = -1;
    break;
  }

  // return len must not larger than bufsize
  if (resplen > bufsize) {
    resplen = bufsize;
  }

  // copy response to stack's buffer if any
  if (response && resplen) {
    memcpy(buffer, response, resplen);
  }

  return resplen;
}

// Callback invoked when received READ10 command.
// Copy disk's data to buffer (up to bufsize) and return number of copied bytes (must be multiple of block size)
extern "C" int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, 
                            void* buffer, uint32_t bufsize)
{
  (void) lun;

  bool rc = SD.card()->readSectors(lba, (uint8_t*) buffer, bufsize/SD_SECTOR_SIZE);

  // only blink fast on reads; writes will override this
  if (MSC_LEDMode == LED_SOLIDON)
    MSC_LEDMode = LED_BLINK_FAST;
  
  return rc ? bufsize : -1;
}

// Callback invoked when receive WRITE10 command.
// Process data in buffer to disk's storage and return number of written bytes (must be multiple of block size)
extern "C" int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset,
                           uint8_t *buffer, uint32_t bufsize) {
  (void) lun;

  bool rc = SD.card()->writeSectors(lba, buffer, bufsize/SD_SECTOR_SIZE);

  // always slow blink
  MSC_LEDMode = LED_BLINK_SLOW;

  return rc ? bufsize : -1;
}

// Callback invoked when WRITE10 command is completed (status received and accepted by host).
// used to flush any pending cache to storage
extern "C" void tud_msc_write10_complete_cb(uint8_t lun) {
  (void) lun;
}

#endif