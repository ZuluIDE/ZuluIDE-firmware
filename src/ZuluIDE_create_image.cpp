
/** 
 * ZuluIDE™ - Copyright (c) 2025 Rabbit Hole Computing™
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
#include <cstring>
#include <ZuluIDE_platform.h>
#include "ZuluIDE_config.h"
#include "ZuluIDE_create_image.h"


extern SdFs SD;

static bool parseCreateCommand(const char *cmd_filename, uint64_t &size, char imgname[MAX_FILE_PATH + 1])
{
  if (strncasecmp(cmd_filename, CREATEFILE, strlen(CREATEFILE)) != 0)
  {
    return false;
  }

  const char *p = cmd_filename + strlen(CREATEFILE);

  // Skip separator if any
  while (isspace(*p) || *p == '-' || *p == '_')
  {
    p++;
  }

  char *unit = nullptr;
  size = strtoul(p, &unit, 10);

  if (size <= 0 || unit <= p)
  {
    logmsg("-- Could not parse size in filename for creating image '", cmd_filename, "'");
    return false;
  }

  // Parse k/M/G unit
  char unitchar = tolower(*unit);
  if (unitchar == 'k')
  {
    size *= 1024;
    p = unit + 1;
  }
  else if (unitchar == 'm')
  {
    size *= 1024 * 1024;
    p = unit + 1;
  }
  else if (unitchar == 'g')
  {
    size *= 1024 * 1024 * 1024;
    p = unit + 1;
  }
  else
  {
    size *= 1024 * 1024;
    p = unit;
  }

  // Skip i and B if part of unit
  if (tolower(*p) == 'i') p++;
  if (tolower(*p) == 'b') p++;

  // Skip separator if any
  while (isspace(*p) || *p == '-' || *p == '_')
  {
    p++;
  }

  // Copy target filename to new buffer
  strncpy(imgname, p, MAX_FILE_PATH);
  imgname[MAX_FILE_PATH] = '\0';
  int namelen = strlen(imgname);

  // Strip .txt extension if any
  if (namelen >= 4 && strncasecmp(imgname + namelen - 4, ".txt", 4) == 0)
  {
    namelen -= 4;
    imgname[namelen] = '\0';
  }

  // Add .bin if no extension
  if (!strchr(imgname, '.') && namelen < MAX_FILE_PATH - 4)
  {
    namelen += 4;
    strcat(imgname, ".bin");
  }

  return true;
}



bool createImageFile(const char *imgname, uint64_t size, uint8_t *write_buf, size_t write_buf_len)
{
  int namelen = strlen(imgname);

  // Check if file exists
  if (namelen <= 5 || SD.exists(imgname))
  {
    logmsg("-- Image file already exists, skipping '", imgname, "'");
    return false;
  }

  // Create file, try to preallocate contiguous sectors
  LED_ON();
  FsFile file = SD.open(imgname, O_WRONLY | O_CREAT);

  if (!file.preAllocate(size))
  {
    logmsg("-- Preallocation didn't find contiguous set of clusters, continuing anyway");
  }

  int blocks = size/write_buf_len;

  // Write zeros to fill the file
  uint32_t start = millis();
  memset(write_buf, 0, write_buf_len);
  uint64_t remain = size;

  int block = 0;
  bool writing_serial_out = false;
  char serial_string[128];
  char *string_marker = serial_string;
  uint32_t seconds = 0;
  uint32_t serial_time = 0;

  while (remain > 0)
  {
    uint32_t time_start = millis();

    if (millis() & 128) { LED_ON(); } else { LED_OFF(); }
    platform_reset_watchdog();

    size_t to_write = write_buf_len;
    if (to_write > remain) to_write = remain;
    if (file.write(write_buf, to_write) != to_write)
    {
      logmsg("-- File writing to '", imgname, "' failed with ", (int)remain, " bytes remaining");
      file.close();
      LED_OFF();
      return false;
    }

    remain -= to_write;
    uint32_t time = (uint32_t)(millis() - start);
    // Create a new string to overwrite the previous line every second
    if(platform_serial_ready() && (time / 1000) > seconds)
    {
      int kb_per_s = (size - remain) / time;
      // "\x1b[2K" is a control charater to clear the current line
      snprintf(serial_string, sizeof(serial_string),"\r\x1b[2KWrote %lu MB with %lu MB remaining at %d kB/s\r", (uint32_t)((size - remain) / 1048576), (uint32_t)(remain / 1048576), kb_per_s);
      string_marker = serial_string;
      writing_serial_out = true;
      seconds++;
    }

    // Attempt write to the serial port every 1/4 second
    if(writing_serial_out && (time / 250) > serial_time)
    {
      uint32_t len = strlen(string_marker);
      uint32_t wrote = 0;
      if (len > 0)
      {
        wrote = platform_serial_write((uint8_t*)string_marker, len);
        string_marker += wrote;
      }
      if (strlen(string_marker) == 0)
      {
        writing_serial_out = false;
      }
      serial_time++;
    }

    block++;
  }

  file.close();
  uint32_t time = millis() - start;
  int kb_per_s = size / time;
  logmsg("-- Image creation successful, write speed ", kb_per_s, " kB/s");

  LED_OFF();
  return true;
}


// When a file is called e.g. "Create_1024M_HD40.txt",
// create image file with specified size.
// Returns true if image file creation succeeded.
//
// Parsing rules:
// - Filename must start with "Create", case-insensitive
// - Separator can be either underscore, dash or space
// - Size must start with a number. Unit of k, kb, m, mb, g, gb is supported,
//   case-insensitive, with 1024 as the base. If no unit, assume MB.
// - If target filename does not have extension (just .txt), use ".bin"
bool createImage(const char *cmd_filename, char imgname[MAX_FILE_PATH + 1], uint8_t *write_buf, size_t write_buf_len)
{
  uint64_t size;

  // Parse the command filename
  if (!parseCreateCommand(cmd_filename, size, imgname))
  {
    return false;
  }

  logmsg("Create image using special file: \"", cmd_filename, "\"");

  // Create the actual image file
  if (!createImageFile(imgname, size, write_buf, write_buf_len))
  {
    return false;
  }

  // Remove the command file after successful creation
  logmsg("-- Image creation successful, removing '", cmd_filename, "'");
  SD.remove(cmd_filename);
  return true;
}

bool searchAndCreateImage(uint8_t *write_buf, size_t write_buf_len)
{
    FsFile root = SD.open("/");
    if (!root.isOpen())
        return false;

    FsFile file;
    bool created = false;
    while(file.openNext(&root))
    {
        char filename[MAX_FILE_PATH + 1] = {};
        char image_name[MAX_FILE_PATH + 1] = {};
        file.getName(filename, sizeof(filename));
        if(createImage(filename, image_name, write_buf, write_buf_len))
        {
            created = true;
        }
    }
    file.close();
    root.close();
    return created;
}