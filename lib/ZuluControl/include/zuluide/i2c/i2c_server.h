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

#pragma once

#include <Wire.h>
#include <zuluide/status/system_status.h>
#include <zuluide/status/device_control_safe.h>
#include <zuluide/images/image_iterator.h>
#include <string>

#define I2C_SERVER_SYSTEM_STATUS_JSON  0xA
#define I2C_SERVER_IMAGE_JSON  0xB
#define I2C_SERVER_POLL_CLIENT 0xC
#define I2C_SERVER_SSID 0xD
#define I2C_SERVER_SSID_PASS 0xE
#define I2C_SERVER_RESET 0xF

#define I2C_CLIENT_NOOP 0x0
#define I2C_CLIENT_SUBSCRIBE_STATUS_JSON  0xA
#define I2C_CLIENT_LOAD_IMAGE  0xB
#define I2C_CLIENT_EJECT_IMAGE  0xC
#define I2C_CLIENT_FETCH_IMAGES_JSON  0xD
#define I2C_CLIENT_FETCH_SSID 0xE
#define I2C_CLIENT_FETCH_SSID_PASS 0xF
#define I2C_CLIENT_FETCH_ITR_IMAGE 0x10

#define CLIENT_ADDR 0x45

using namespace zuluide::status;

namespace zuluide::i2c {
  /**
     Manages communication with an I2C client sending it status and alloweding it
     to request operations.

     Data is sent to the I2C client from this server by writing length prefaced
     strings to the registers defined in I2C_SERVER_* defines.

     Data is sent to the server from the I2C client by the server reading a command,
     (defined in I2C_CLIENT_*) byte from the client. A length prefaced string is
     then read from the I2C client.
   */
  class I2CServer {
  public:
    /**
       Default constructor.
     */
    I2CServer();
    void Init(TwoWire* wire, DeviceControlSafe* deviceControl);
    /**
       Handle updates to the system status. In practice, if an I2C client is
       subscribed to updates, a JSON representation of the system state is built
       and sent.
     */
    void HandleUpdate(const SystemStatus& current);
    /**
       Polls the I2C client to see if they have data (commands) they want to
       send to the server and also processes the reqeusts.
     */
    void Poll();
    /**
       Stores the SSID that is to be passed to the I2C client.
     */
    void SetSSID(std::string& value);
    /**
       Stores the Wifi password to be passed ot the I2C client.
    */
    void SetPassword(std::string &value);
    /**
       Sends a reset command to the I2C client. Returns true if the send is
       succesful, otherwise false.
     */
    bool CheckForDevice();
  private:
    TwoWire* wire;
    DeviceControlSafe* deviceControl;
    bool isSubscribed;
    bool initialized;
    bool sendFiles;
    bool sendNextImage;
    bool isIterating;
    bool isPresent;
    zuluide::images::ImageIterator iterator;
    std::string status;
    std::string ssid;
    std::string password;
  };
}
