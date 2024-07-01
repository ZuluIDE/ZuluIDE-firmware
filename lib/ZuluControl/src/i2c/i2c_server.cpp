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
#include <zuluide/images/utils.h>
#include "ZuluIDE_log.h"
#include "ZuluIDE_platform.h"

#ifndef BUFFER_LENGTH
#define BUFFER_LENGTH 8
#endif

using namespace zuluide::i2c;

I2CServer::I2CServer() : deviceControl(nullptr), isSubscribed(false), initialized(false), sendFiles(false), sendNextImage(false), isIterating(false), isPresent(false) {
}

void I2CServer::Init(TwoWire* wireValue, DeviceControlSafe* devControl) {
  wire = wireValue;
  deviceControl = devControl;
  initialized = true;
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
  status = current.ToJson();
  if (isSubscribed) {
    writeLengthPrefacedString(wire, I2C_SERVER_SYSTEM_STATUS_JSON, status.length(), status.c_str());
  } else {
    logmsg("Received an update, but I2C client is not subscribed.");
  }
}

static uint16_t ReadInLength(TwoWire *wire) {
  // Read in the length.
  uint16_t length = wire->read();
  length = length << 8;
  length |= wire->read();
  return length;
}

void I2CServer::Poll() {
  if (!initialized || !isPresent) {
    return;
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
  case I2C_CLIENT_SUBSCRIBE_STATUS_JSON: {
    wire->requestFrom(CLIENT_ADDR, 2);
    if (ReadInLength(wire) != 0) {
      logmsg("Length was not 0 for subscribe request.");
    }
    
    logmsg("I2C Client subscribed to updates.");
    isSubscribed = true;    
    
    // Send over the current status.
    writeLengthPrefacedString(wire, I2C_SERVER_SYSTEM_STATUS_JSON, status.length(), status.c_str());

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
          logmsg("Recvd ", buffer[pos - 1]);
        }
      }

      logmsg("Client requested the current image be set to:", buffer);

      // Load the image.
      zuluide::images::Image toLoad(std::string(), 0);
      if (zuluide::images::LoadImageByFileName(buffer, &toLoad)) {
        deviceControl->LoadImageSafe(toLoad);
      }

      delete[] buffer;
    }

    break;
  }

  case I2C_CLIENT_EJECT_IMAGE: {
    wire->requestFrom(CLIENT_ADDR, 2);
    if (ReadInLength(wire) != 0) {
      logmsg("Length was not 0 for eject image request.");
    }

    deviceControl->EjectImageSafe();
    break;
  }

  case I2C_CLIENT_FETCH_IMAGES_JSON: {
    wire->requestFrom(CLIENT_ADDR, 2);
    if (ReadInLength(wire) != 0) {
      logmsg("Length was not 0 for fetch images request.");
    }
    
    logmsg("Client is fetching images. core");
    sendFiles = true;
    break;
  }

  case I2C_CLIENT_FETCH_ITR_IMAGE: {
    wire->requestFrom(CLIENT_ADDR, 2);
    if (ReadInLength(wire) != 0) {
      logmsg("Length was not 0 for fetch iterate image request.");
    }
    
    logmsg("Client is fetching iterate image.");
    sendNextImage = true;
    
    break;
  }

  case I2C_CLIENT_FETCH_SSID: {
    wire->requestFrom(CLIENT_ADDR, 2);
    if (ReadInLength(wire) != 0) {
      logmsg("Length was not 0 for fetch ssid request.");
    }
        
    writeLengthPrefacedString(wire, I2C_SERVER_SSID, ssid.length(), ssid.c_str());
    break;
  }

  case I2C_CLIENT_FETCH_SSID_PASS: {
    wire->requestFrom(CLIENT_ADDR, 2);
    if (ReadInLength(wire) != 0) {
      logmsg("Length was not 0 for fetch ssid pass request.");
    }
    
    writeLengthPrefacedString(wire, I2C_SERVER_SSID_PASS, password.length(), password.c_str());
    break;
  }

  case I2C_CLIENT_NOOP: {
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
