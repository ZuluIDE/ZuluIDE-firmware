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
#include <hardware/i2c.h>
#include <pico/time.h>
#include <zuluide/status/system_status.h>
#include <zuluide/status/device_control_safe.h>
#include <zuluide/pipe/image_request_pipe.h>
#include <zuluide/pipe/image_response_pipe.h>
#include "i2c_server_src_type.h"
#include <string>

#define I2C_API_VERSION "4.0.0"

// Delay between reading the filenames off the SD card in milliseconds
#ifndef I2C_FILENAME_TRANSFER_DELAY
#define I2C_FILENAME_TRANSFER_DELAY 200
#endif

#define I2C_SERVER_API_VERSION  0x1
#define I2C_SERVER_WIFI_CONNECT 0x2
#define I2C_SERVER_UPDATE_FW_REQUEST 0x3   // Server requests the client to start, stop, or abort a firmware upgrade
#define I2C_SERVER_UPDATE_FW_DATA 0x4      // Server sends a chunk of firmware data to the client
#define I2C_SERVER_UPDATE_FILENAME_CACHE 0x8
#define I2C_SERVER_IMAGE_FILENAME 0x9
#define I2C_SERVER_SYSTEM_STATUS_JSON  0xA
#define I2C_SERVER_IMAGE_JSON  0xB
#define I2C_SERVER_POLL_CLIENT 0xC
#define I2C_SERVER_SSID 0xD
#define I2C_SERVER_SSID_PASS 0xE
#define I2C_SERVER_RESET 0xF
#define I2C_SERVER_STATIC_IP 0x10
#define I2C_SERVER_IP_ADDRESS_ACK 0x11
#define I2C_SERVER_SD_STATUS_CHANGE 0x13  // SD card presence changed; payload[0] = 0x00 not present, 0x01 present

#define I2C_SERVER_SD_NOT_PRESENT 0x00
#define I2C_SERVER_SD_PRESENT     0x01

#define I2C_SERVER_FW_UPGRADE_START 0x00
#define I2C_SERVER_FW_UPGRADE_FINISH 0x01
#define I2C_SERVER_FW_UPGRADE_ABORT 0x02
#define I2C_SERVER_FW_UPGRADE_RETRY 0x03

#define I2C_CLIENT_NOOP 0x0

#define I2C_CLIENT_API_VERSION 0x01
#define I2C_CLIENT_UPDATE_FW_ACK 0x03
#define I2C_CLIENT_ACK_SPECIAL_LEN 0xFFFF
#define I2C_CLIENT_ACK_CRC_UPGRADE_COMPLETE 0x00000000

#define I2C_CLIENT_FETCH_FILENAMES 0x09
#define I2C_CLIENT_SUBSCRIBE_STATUS_JSON  0xA
#define I2C_CLIENT_LOAD_IMAGE  0xB
#define I2C_CLIENT_EJECT_IMAGE  0xC
#define I2C_CLIENT_FETCH_IMAGES_JSON  0xD
#define I2C_CLIENT_FETCH_SSID 0xE
#define I2C_CLIENT_FETCH_SSID_PASS 0xF
#define I2C_CLIENT_FETCH_ITR_IMAGE 0x10
#define I2C_CLIENT_IP_ADDRESS 0x11
#define I2C_CLIENT_LOG_MSG 0x12

#define CLIENT_ADDR 0x45

#define I2C_UPGRADE_MAX_CHUNK 2048

#define I2C_NORMAL_BUS_SPEED 100000
#define I2C_FAST_BUS_SPEED 400000

enum class State { 
                   WaitForAPIVersion,
                   WaitingForSSID,
                   WaitingForPassword,
                   WIFIInit,
                   WIFIDown,
                   Normal };




namespace ClientMessage {
  namespace Prefix
  {
    constexpr char Normal  = 'n';
    constexpr char Debug   = 'd';
    constexpr char Unknown = 'u';
  }

  enum class Type {
                    Normal,
                     Debug
};
}


using namespace zuluide::status;
using namespace zuluide::pipe;

namespace zuluide::i2c {
  
  /**
     Manages communication with an I2C client sending it status and allowing it
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

    /**
       Provides the raw pico-sdk i2c_inst_t backing the same bus as SetI2c's TwoWire
       (Wire's own instance pointer is private, with no accessor), so the DMA-driven
       firmware-upgrade transport can issue raw pico-sdk calls interleaved with Wire's
       own blocking calls on the same peripheral. Must be called once, after SetI2c,
       before any firmware upgrade is attempted.
     */
    void SetI2cRaw(i2c_inst_t* i2c);

    void SetDeviceControl(DeviceControlSafe* deviceControl);
    /**
       Handle updates to the system status. In practice, if an I2C client is
       subscribed to updates, a JSON representation of the system state is built
       and sent.
     */

    /**
     * CheckForDevice will bypass checking the I2C connection and assume
     * there is a device, even if later I2C calls may fail while the I2C
     * connection is being reset.
     */
    void ForceIsPresent();

    void HandleUpdate(const SystemStatus& current);

    /**
       Polls the I2C client to see if they have data (commands) they want to
       send to the server and also processes the requests.
     */
    void Poll();
    /**
       Stores the SSID that is to be passed to the I2C client.
     */
    void SetSSID(const std::string& value);
    /**
       Stores the Wifi password to be passed ot the I2C client.
    */
    void SetPassword(const std::string &value);
    /**
       Stores the Wifi static IPv4 to be passed ot the I2C client.
    */
    void SetIPv4(const std::string &value);
    /**
       Stores the Wifi static IP netmask to be passed ot the I2C client.
    */
    void SetNetmask(const std::string &value);
    /**
       Stores the Wifi static IP gateway to be passed ot the I2C client.
    */
    void SetGateway(const std::string &value);
    /**
       Sends a reset command to the I2C client. Returns true if the send is
       successful, otherwise false.
     */
    bool CheckForDevice();
    /**
       True if the SSID has been set.
     */
    bool WiFiSSIDSet();

    /**
       True if the password has been set.
     */
    bool WiFiPasswordSet();

    /**
       True if the I2C was not interrupted
     */
    bool WiFiConnect();

    /**
        Requests I2C client to consume filenames from this I2C server
     */
    void UpdateFilenames();

    enum UpgradeRequest
    {
      START = 0x0,
      FINISH,
      ABORT,
      RETRY
    };
    /**
      Send ZuluControl-firmware upgrade to start, finish, abort, or retry the upgrade
      process. Always travels over the existing Wire transport, like every other
      message in this protocol.
     */
    void UpgradeZuluControlFwRequest(UpgradeRequest request);

    /**
     * Sends a chunk of the ZuluControl-firmware via DMA (proven to work on the
     * host's master-mode transmit side)
     * Returns false on any I2C-level failure (NACK/timeout); *outSentCrc
     * is only valid when this returns true.
     */
    bool UpgradeZuluControlFwSendChunk(const uint8_t* buffer, size_t length, uint32_t* outSentCrc, absolute_time_t until);

    /**
     * Reads the client's {length, crc32} ack for a previously sent chunk, over the
     * same Wire-based request/response mechanism used for every other client
     * response (see Poll()). The client's core0 needs a moment after the chunk
     * transaction completes to dequeue it, compute the CRC, and enqueue the ack --
     * until then, its request handler replies I2C_CLIENT_NOOP (its normal "nothing
     * queued yet" response). So this polls, retrying on NOOP, until either the ack
     * arrives or `until` is reached. Returns false on I2C-level failure (NACK) or
     * on timeout.
     */
    bool UpgradeZuluControlFwReadAck(uint16_t* outLength, uint32_t* outCrc, absolute_time_t until);

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
     * Sends WiFi connection request
     */
    void RequestWiFiConnect(const i2c_server_source_t source);
    /**
     * Sends a clean up iterator request
     */
    void RequestCleanup(const i2c_server_source_t source);

    /**
     * Sends a reset iterator request
     */
    void RequestReset(const i2c_server_source_t source);

    void EnterLoggingSafe();
    void ExitLoggingSafe();

  private:
    zuluide::pipe::ImageRequestPipe<i2c_server_source_t>* imageRequestPipe;
    zuluide::pipe::ImageResponsePipe<i2c_server_source_t>* imageResponsePipe;
    enum class FilenameTransferState {Idle, Start, Sending, Received} filenameTransferState;
    TwoWire* wire;
    i2c_inst_t* i2cRaw;
    DeviceControlSafe* deviceControl;
    bool isSubscribed;
    bool apiVersionSent;
    bool devControlSet;
    bool sendFilenames;
    bool sendFiles;
    bool sendNextImage;
    bool updateFilenameCache;
    bool isIterating;
    bool isPresent;
    bool forcePresent;
    bool lastCardPresent;
    std::string status;
    std::string ssid;
    std::string password;
    std::string ip;
    std::string netmask;
    std::string gateway;
    unsigned long remoteMajorVersion;
    std::string remoteVersionString;
  };
}
