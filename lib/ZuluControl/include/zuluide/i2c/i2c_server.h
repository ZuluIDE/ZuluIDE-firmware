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
 * Under Section 7 of GPL version 3, you are granted additional
 * permissions described in the ZuluIDE Hardware Support Library Exception
 * (GPL-3.0_HSL_Exception.md), as published by Rabbit Hole Computing™.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
**/

#pragma once

#include <Wire.h>
#include <zuluide/status/system_status.h>
#include <zuluide/status/device_control_safe.h>
#include <zuluide/pipe/image_request_pipe.h>
#include <zuluide/pipe/image_response_pipe.h>
#include "i2c_server_src_type.h"
#include <string>

#define I2C_API_VERSION "3.0.0"

// Delay between reading the filenames off the SD card in milliseconds
#ifndef I2C_FILENAME_TRANSFER_DELAY
#define I2C_FILENAME_TRANSFER_DELAY 200
#endif

#define I2C_SERVER_API_VERSION  0x1
#define I2C_SERVER_UPDATE_FILENAME_CACHE 0x8
#define I2C_SERVER_IMAGE_FILENAME 0x9
#define I2C_SERVER_SYSTEM_STATUS_JSON  0xA
#define I2C_SERVER_IMAGE_JSON  0xB
#define I2C_SERVER_POLL_CLIENT 0xC
#define I2C_SERVER_SSID 0xD
#define I2C_SERVER_SSID_PASS 0xE
#define I2C_SERVER_RESET 0xF

#define I2C_CLIENT_NOOP 0x0

#define I2C_CLIENT_API_VERSION 0x01
#define I2C_CLIENT_FETCH_FILENAMES 0x09
#define I2C_CLIENT_SUBSCRIBE_STATUS_JSON  0xA
#define I2C_CLIENT_LOAD_IMAGE  0xB
#define I2C_CLIENT_EJECT_IMAGE  0xC
#define I2C_CLIENT_FETCH_IMAGES_JSON  0xD
#define I2C_CLIENT_FETCH_SSID 0xE
#define I2C_CLIENT_FETCH_SSID_PASS 0xF
#define I2C_CLIENT_FETCH_ITR_IMAGE 0x10
#define I2C_CLIENT_IP_ADDRESS 0x11
#define I2C_CLIENT_NET_DOWN 0x12

#define CLIENT_ADDR 0x45

using namespace zuluide::status;
using namespace zuluide::pipe;

namespace zuluide::i2c {
  
  /**
     Manages communication with an I2C client sending it status and alloweing it
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
    I2CServer(zuluide::pipe::ImageRequestPipe<i2c_server_source_t>* image_request_pipe, zuluide::pipe::ImageResponsePipe<i2c_server_source_t>* image_response_pipe);
  
    void SetI2c(TwoWire* wire);

    void SetDeviceControl(DeviceControlSafe* deviceControl);
    /**
       Handle updates to the system status. In practice, if an I2C client is
       subscribed to updates, a JSON representation of the system state is built
       and sent.
     */
    void HandleUpdate(const SystemStatus& current);
    /**
       Polls the I2C client to see if they have data (commands) they want to
       send to the server and also processes the requests.
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
    /**
       True if the SSID and and password have been set.
     */
    bool WifiCredentialsSet();

    /**
        Requests I2C client to consume filenames from this I2C server
     */
    void UpdateFilenames();

    /**
     * Observer that handles responses. Called from notifyObservers which should be run on the core without SD access
     */
    void HandleImageResponse(const zuluide::pipe::ImageResponse<i2c_server_source_t>& response);

    /**
     * Handles the different i2c_server_source_t types from HandleImageResponse
     */
    void HandleFetchFilenames(const std::unique_ptr<ImageResponse<i2c_server_source_t>>&& response);
    void HandleFetchImages(const std::unique_ptr<ImageResponse<i2c_server_source_t>>&& response);
    void HandleFetchImage(const std::unique_ptr<ImageResponse<i2c_server_source_t>>&& response);
    void HandleSetToCurrent(const std::unique_ptr<ImageResponse<i2c_server_source_t>>&& response);
    /**
     * Sends a clean up iterator request
     */
    void RequestCleanup(const i2c_server_source_t source);

    /**
     * Sends a reset iterator request
     */
    void RequestReset(const i2c_server_source_t source);

  private:
    zuluide::pipe::ImageRequestPipe<i2c_server_source_t>* imageRequestPipe;
    zuluide::pipe::ImageResponsePipe<i2c_server_source_t>* imageResponsePipe;  
    enum class FilenameTransferState {Idle, Start, Sending, Received} filenameTransferState;
    TwoWire* wire;
    DeviceControlSafe* deviceControl;
    bool isSubscribed;
    bool devControlSet;
    bool sendFilenames;
    bool sendFiles;
    bool sendNextImage;
    bool updateFilenameCache;
    bool isIterating;
    bool isPresent;
    std::string status;
    std::string ssid;
    std::string password;
    unsigned long remoteMajorVersion;
    std::string remoteVersionString;
  };
}
