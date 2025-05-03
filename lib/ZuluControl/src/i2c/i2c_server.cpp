/**
 * ZuluIDE™ - Copyright (c) 2024 Rabbit Hole Computing™
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

#include <zuluide/i2c/i2c_server.h>
#include <sstream>
#include <cctype>
#include "ZuluIDE_log.h"
#include "ZuluIDE_platform.h"

#ifndef BUFFER_LENGTH
#define BUFFER_LENGTH 8
#endif

using namespace zuluide::i2c;

I2CServer::I2CServer() : filenameTransferState(FilenameTransferState::Idle), deviceControl(nullptr), isSubscribed(false), devControlSet(false), sendFilenames(false), sendFiles(false), sendNextImage(false), updateFilenameCache(false), isIterating(false), isPresent(false), remoteMajorVersion(0){
}

void I2CServer::SetI2c(TwoWire* wireValue) {
  wire = wireValue;
}

void I2CServer::SetDeviceControl(DeviceControlSafe* devControl) 
{
    deviceControl = devControl;
    devControlSet = true;
}
bool writeLengthPrefacedString(TwoWire *wire, uint8_t reg, uint16_t length, const char* buffer) {
  wire->beginTransmission(CLIENT_ADDR);
  wire->write(reg);
  
  // Send the length of the string as a two byte unsigned integer.
  uint8_t high = length >> 8;
  uint8_t low = length & 0xFF;
  wire->write(high);
  wire->write(low);
  if (wire->endTransmission() != 0) {
    return false;
  }

  // Break the string into BUFFER_LENGTH sized transmissions.
  // Larger values for BUFFER_LENGTH have not worked.
  for (int pos = 0; pos < length; pos += BUFFER_LENGTH) {
    wire->beginTransmission(CLIENT_ADDR);
    int toSend = (pos + BUFFER_LENGTH) < length ? BUFFER_LENGTH : length - pos;
    wire->write(buffer + pos, toSend);
    wire->endTransmission();
  }

  return true;
}

bool I2CServer::CheckForDevice() {
  auto buf = "";
  isPresent = writeLengthPrefacedString(wire, I2C_SERVER_RESET, 0, buf);
  return isPresent;
}
void I2CServer::HandleUpdate(const SystemStatus& current) {
  static bool lastCardPresentStatus = false;
  bool card_present = current.IsCardPresent();
  if (lastCardPresentStatus != card_present) {
    logmsg("============== detected SD card presense change ==============");
    updateFilenameCache = true;
  }

  status = current.ToJson();
  if (isSubscribed)
    writeLengthPrefacedString(wire, I2C_SERVER_SYSTEM_STATUS_JSON, status.length(), status.c_str());
}

static uint16_t ReadInLength(TwoWire *wire) {
  // Read in the length.
  uint16_t length = wire->read();
  length = length << 8;
  length |= wire->read();
  return length;
}

void I2CServer::Poll() {
  if (!devControlSet || !isPresent) {
    return;
  }

  if (updateFilenameCache && isSubscribed) {
    mutex_enter_blocking(platform_get_log_mutex());
    logmsg("Requesting client to update their cache of filenames");
    mutex_exit(platform_get_log_mutex());
    updateFilenameCache = false;
    filenameTransferState = FilenameTransferState::Start;
    auto buf = "";
    writeLengthPrefacedString(wire, I2C_SERVER_UPDATE_FILENAME_CACHE, 0, buf);
  }

  static uint32_t filenameXferTime = 0;
  static uint32_t filenameXferWait = 2000;
  static bool first_run = true;
  
  if (first_run)
  {
    first_run = false;
    filenameXferTime = millis();
    mutex_enter_blocking(platform_get_log_mutex());
    mutex_exit(platform_get_log_mutex());

  }

  if ( isSubscribed
      && (filenameTransferState == FilenameTransferState::Start 
        || filenameTransferState == FilenameTransferState::Sending)
      && ((uint32_t)(millis() - filenameXferTime) > filenameXferWait))
    {
    filenameXferTime = millis();
    filenameXferWait = 150;
    if (filenameTransferState == FilenameTransferState::Start) {
      fn_iterator.Reset();
      filenameTransferState = FilenameTransferState::Sending;
    }
    else
    {
      mutex_enter_blocking(platform_get_log_mutex());
      if(fn_iterator.MoveNext()) {
        auto msgBuf = fn_iterator.Get().GetFilename();
        // logmsg("Sending filename: ", msgBuf.c_str());
        writeLengthPrefacedString(wire, I2C_SERVER_IMAGE_FILENAME, msgBuf.size(), msgBuf.c_str());
      } else {
        filenameTransferState = FilenameTransferState::Idle;
        auto emptyMsgBuf = "";
        // logmsg("Sending end of filenames as empty string");
        writeLengthPrefacedString(wire, I2C_SERVER_IMAGE_FILENAME, 0, emptyMsgBuf);
        iterator.Cleanup();
      }
      mutex_exit(platform_get_log_mutex());
    }
  }

  if (sendFiles) {
    mutex_enter_blocking(platform_get_log_mutex());
    iterator.Reset();
    while (iterator.MoveNext()) {
      sendFiles = false;
      auto msgBuf = iterator.Get().ToJson();
      logmsg("Sending image: ", msgBuf.c_str());
      writeLengthPrefacedString(wire, I2C_SERVER_IMAGE_JSON, msgBuf.size(), msgBuf.c_str());
    }

    auto emptyMsgBuf = "";
    logmsg("Sending end of images as empty string");
    writeLengthPrefacedString(wire, I2C_SERVER_IMAGE_JSON, 0, emptyMsgBuf);

    iterator.Cleanup();
    mutex_exit(platform_get_log_mutex());
  }

  if (sendNextImage) {
    mutex_enter_blocking(platform_get_log_mutex());    
    if (!isIterating) {
      isIterating = true;
      iterator.Reset();
    }
    
    if (iterator.MoveNext()) {
      auto msgBuf = iterator.Get().ToJson();
      logmsg("Sending image: ", msgBuf.c_str());
      writeLengthPrefacedString(wire, I2C_SERVER_IMAGE_JSON, msgBuf.size(), msgBuf.c_str());
    } else {
      auto msgBuf = "";
      logmsg("Sending end of images as empty string");
      writeLengthPrefacedString(wire, I2C_SERVER_IMAGE_JSON, 0, msgBuf);
      isIterating = false;
      iterator.Cleanup();
    }
    
    sendNextImage = false;
    mutex_exit(platform_get_log_mutex());
  }

  wire->requestFrom(CLIENT_ADDR, 1);
  uint8_t requestType = wire->read();

  switch (requestType) {
  case I2C_CLIENT_API_VERSION:
  {
    mutex_enter_blocking(platform_get_log_mutex()); 
    wire->requestFrom(CLIENT_ADDR, 2);
    uint16_t length = ReadInLength(wire);
    if (length > 0)
    {
      // Client is sending some data.
      char* buffer = new char[length + 1];
      memset(buffer, 0, length + 1);

      for (int pos = 0; pos < length;)
      {
        int toRecv = pos + BUFFER_LENGTH < length ? BUFFER_LENGTH : length - pos;

        wire->requestFrom(CLIENT_ADDR, toRecv);
        while (toRecv > 0) {
          while (wire->available() == 0) { }
          buffer[pos++] = wire->read();
          toRecv--;
        }
      }

      bool major_version_match = false;
      if (buffer[0] != '\0')
      {
        unsigned long local_major_version;
        char* period_location = strchr(buffer, '.');
        if (period_location != NULL)
        {
          remoteVersionString = buffer;
          remoteMajorVersion = strtoul(buffer, &period_location, 10);
          period_location = strchr(I2C_API_VERSION, '.');
          if (period_location != NULL)
          {
            local_major_version = strtoul(I2C_API_VERSION, &period_location, 10);
            if (local_major_version > 0 && local_major_version == remoteMajorVersion)
            {
              major_version_match = true;
            }
          }
        }
      }

      if (major_version_match)
      {
        dbgmsg("I2C server and client major version match. Client: v",remoteVersionString.c_str()," Server: v", I2C_API_VERSION);
      }
      else
      {
        if (remoteMajorVersion > 0)
          logmsg("I2C server (v",I2C_API_VERSION ,") and client major version (v",remoteVersionString.c_str(),") mismatch. Please upgrade both devices to the latest firmware");
        else
          logmsg("I2C client failed to send API version I2C server API verion. Please upgrade both devices to the latest firmware");

      }
      writeLengthPrefacedString(wire, I2C_SERVER_API_VERSION, strlen(I2C_API_VERSION), I2C_API_VERSION);
      delete[] buffer;
    }
    mutex_exit(platform_get_log_mutex());
    break;
  }

  case I2C_CLIENT_SUBSCRIBE_STATUS_JSON: {
    mutex_enter_blocking(platform_get_log_mutex()); 
    wire->requestFrom(CLIENT_ADDR, 2);
    if (ReadInLength(wire) != 0) {
      logmsg("Length was not 0 for subscribe request.");
    }
    
    logmsg("I2C Client subscribed to updates.");
    isSubscribed = true;    
    
    // Send over the current status.
    writeLengthPrefacedString(wire, I2C_SERVER_SYSTEM_STATUS_JSON, status.length(), status.c_str());
    mutex_exit(platform_get_log_mutex());
    break;
  }

  case I2C_CLIENT_LOAD_IMAGE: {
    wire->requestFrom(CLIENT_ADDR, 2);
    uint16_t length = ReadInLength(wire);

    if (length > 0) {
      // Client is sending some data.
      char* buffer = new char[length + 1];
      memset(buffer, 0, length + 1);

      for (int pos = 0; pos < length;) {        
        int toRecv = pos + BUFFER_LENGTH < length ? BUFFER_LENGTH : length - pos;

        wire->requestFrom(CLIENT_ADDR, toRecv);
        while (toRecv > 0) {
          while (wire->available() == 0) { }
          buffer[pos++] = wire->read();
          toRecv--;
        }
      }
      if (buffer[0] != '\0')
      {

        mutex_enter_blocking(platform_get_log_mutex());
        logmsg("Client requested the current image be set to:", buffer);
        iterator.Reset();
        bool found_file = iterator.MoveToFile(buffer);
        zuluide::images::Image toLoad(std::string(), 0);
        toLoad = iterator.Get();
        iterator.Cleanup();
        mutex_exit(platform_get_log_mutex());
        if (found_file) {
          deviceControl->LoadImageSafe(toLoad);
        }
      }

      delete[] buffer;
    }

    break;
  }

  case I2C_CLIENT_EJECT_IMAGE: {

    wire->requestFrom(CLIENT_ADDR, 2);
    if (ReadInLength(wire) != 0) {
      mutex_enter_blocking(platform_get_log_mutex());
      logmsg("Length was not 0 for eject image request.");
      mutex_exit(platform_get_log_mutex());
    }

    deviceControl->EjectImageSafe();
    break;
  }

  case I2C_CLIENT_FETCH_FILENAMES: {
    wire->requestFrom(CLIENT_ADDR, 2);
    mutex_enter_blocking(platform_get_log_mutex());
    if (ReadInLength(wire) != 0) {
      logmsg("Length was not 0 for fetch filenames request.");
    }
    
    logmsg("Client is fetching filenames.");
    mutex_exit(platform_get_log_mutex());
    sendFilenames = true;
    break;
  }
  
  case I2C_CLIENT_FETCH_IMAGES_JSON: {
    wire->requestFrom(CLIENT_ADDR, 2);
    mutex_enter_blocking(platform_get_log_mutex());
    if (ReadInLength(wire) != 0) {
      logmsg("Length was not 0 for fetch images request.");
    }
    
    logmsg("Client is fetching images.");
    mutex_exit(platform_get_log_mutex());
    sendFiles = true;
    break;
  }

  case I2C_CLIENT_FETCH_ITR_IMAGE: {
    wire->requestFrom(CLIENT_ADDR, 2);
    mutex_enter_blocking(platform_get_log_mutex());
    if (ReadInLength(wire) != 0) {
      logmsg("Length was not 0 for fetch iterate image request.");
    }
    
    logmsg("Client is fetching iterate image.");
    mutex_exit(platform_get_log_mutex());
    sendNextImage = true;
    
    break;
  }

  case I2C_CLIENT_FETCH_SSID: {
    wire->requestFrom(CLIENT_ADDR, 2);
    mutex_enter_blocking(platform_get_log_mutex());
    if (ReadInLength(wire) != 0) {
      logmsg("Length was not 0 for fetch ssid request.");
    }

    if (ssid.length() == 0) {
      logmsg("Client requested the WiFi SSID, but the SSID is not configured.");
    }
    mutex_exit(platform_get_log_mutex());
    writeLengthPrefacedString(wire, I2C_SERVER_SSID, ssid.length(), ssid.c_str());
    break;
  }

  case I2C_CLIENT_FETCH_SSID_PASS: {
    wire->requestFrom(CLIENT_ADDR, 2);
    mutex_enter_blocking(platform_get_log_mutex());
    if (ReadInLength(wire) != 0) {
      logmsg("Length was not 0 for fetch ssid pass request.");
    }

    if (password.length() == 0) {
      logmsg("Client requested SSID password, but the SSID password is not configured.");
    }
    mutex_exit(platform_get_log_mutex());
    writeLengthPrefacedString(wire, I2C_SERVER_SSID_PASS, password.length(), password.c_str());
    break;
  }

  case I2C_CLIENT_NOOP: {
    break;
  }

  case I2C_CLIENT_IP_ADDRESS: {
    wire->requestFrom(CLIENT_ADDR, 2);
    uint16_t length = ReadInLength(wire);

    if (length > 0) {
      // Client is sending some data.
      char* buffer = new char[length + 1];
      memset(buffer, 0, length + 1);

      for (int pos = 0; pos < length;) {        
        int toRecv = pos + BUFFER_LENGTH < length ? BUFFER_LENGTH : length - pos;

        wire->requestFrom(CLIENT_ADDR, toRecv);
        while (toRecv > 0) {
          while (wire->available() == 0) { }
          buffer[pos++] = wire->read();
          toRecv--;
        }
      }
      mutex_enter_blocking(platform_get_log_mutex());
      logmsg("Client IP address is: ", buffer);
      mutex_exit(platform_get_log_mutex());
    }
    
    break;
  }
    
  case I2C_CLIENT_NET_DOWN: {
    wire->requestFrom(CLIENT_ADDR, 2);
    mutex_enter_blocking(platform_get_log_mutex());
    if (ReadInLength(wire) != 0) {
      logmsg("Length was not 0 for NET_DOWN request/notification.");
    }

    logmsg("Client network is down.");
    mutex_exit(platform_get_log_mutex());
    break;
  }

  default: {    
    break;
  }
  }
}

void I2CServer::SetSSID(std::string& value) {
  ssid = value;
}

void I2CServer::SetPassword(std::string &value) {
  password = value;
}

bool I2CServer::WifiCredentialsSet() {
  return !ssid.empty() && !password.empty();
}


void I2CServer::UpdateFilenames() {
  mutex_enter_blocking(platform_get_log_mutex());
  logmsg("Sending request to client to update the filenames");
  mutex_exit(platform_get_log_mutex());
  auto emptyMsgBuf = "";
  writeLengthPrefacedString(wire, I2C_SERVER_UPDATE_FILENAME_CACHE, 0, emptyMsgBuf);
}