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

#include <zuluide/pipe/image_response.h>
#include <sstream>
#include <cctype>
#include "ZuluIDE_log.h"
#include "ZuluIDE_platform.h"
#include <ZuluControl_platform.h>
#include <ide_protocol.h>
#include "zuluide/i2c/i2c_server.h"
#include "zuluide/i2c/i2c_server_src_type.h"
#include "zuluide/i2c/i2c_master_dma.h"


#ifndef BUFFER_LENGTH
#define BUFFER_LENGTH 8
#endif

using namespace zuluide::pipe;
using namespace zuluide::i2c;



I2CServer::I2CServer(ImageRequestPipe<i2c_server_source_t>* image_request_pipe, ImageResponsePipe<i2c_server_source_t>* image_response_pipe) :
  imageRequestPipe(image_request_pipe), imageResponsePipe(image_response_pipe),
  filenameTransferState(FilenameTransferState::Idle), i2cRaw(nullptr), deviceControl(nullptr),
  isSubscribed(false), apiVersionSent(false), devControlSet(false), sendFilenames(false), sendFiles(false),
  sendNextImage(false), updateFilenameCache(false), isIterating(false), isPresent(false), forcePresent(false), lastCardPresent(false), password(""), ip(""), netmask(""), gateway(""), remoteMajorVersion(0)
{
  imageResponsePipe->AddObserver([&](const ImageResponse<i2c_server_source_t>& t){HandleImageResponse(t);});
}

void I2CServer::SetI2c(TwoWire* wireValue) {
  wire = wireValue;
}

void I2CServer::SetI2cRaw(i2c_inst_t* i2c) {
  i2cRaw = i2c;
  I2CMasterDmaInit(i2cRaw);
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
  uint8_t wire_status = wire->endTransmission();

  if (wire_status != 0) {
    dbgmsg("-- I2C transmission error: ", (int)wire_status);
    return false;
  }
  // Break the string into BUFFER_LENGTH sized transmissions.
  // Larger values for BUFFER_LENGTH have not worked.
  for (int pos = 0; pos < length; pos += BUFFER_LENGTH) {
#ifndef CONTROL_CROSS_CORE_QUEUE
     ide_protocol_poll();
#endif
    wire->beginTransmission(CLIENT_ADDR);
    int toSend = (pos + BUFFER_LENGTH) < length ? BUFFER_LENGTH : length - pos;
    wire->write(buffer + pos, toSend);
    wire_status = wire->endTransmission();

    if (wire_status != 0)
    {
      logmsg("-- I2C data transmission error: ", (int)wire_status);
      return false;
    }
  }

  return true;
}

extern uint32_t g_i2c_bus_speed;
bool I2CServer::CheckForDevice() {
  apiVersionSent = false;
  auto buf = "";
  if (forcePresent)
  {
    writeLengthPrefacedString(wire, I2C_SERVER_RESET, 0, buf);
    isPresent = true;
  }
  else
  {
    isPresent = writeLengthPrefacedString(wire, I2C_SERVER_RESET, 0, buf);
  }
  if (!isPresent && g_i2c_bus_speed != I2C_NORMAL_BUS_SPEED)
  {
    wire->setClock(I2C_NORMAL_BUS_SPEED);
    isPresent =  writeLengthPrefacedString(wire, I2C_SERVER_RESET, 0, buf);
    if (isPresent)
    {
      g_i2c_bus_speed = I2C_NORMAL_BUS_SPEED;
      logmsg("I2C successfully fell back to normal bus speed (100kHz)");
    }
    else
    {
      wire->setClock(g_i2c_bus_speed);
    }
  }

  if (isPresent) {
    static const char ide_api_ver[] = I2C_API_VERSION " ZuluIDE";
    if (writeLengthPrefacedString(wire, I2C_SERVER_API_VERSION, strlen(ide_api_ver), ide_api_ver)) {
      apiVersionSent = true;
    }
  }
  return isPresent;
}

void I2CServer::ForceIsPresent() {
  forcePresent = true;
}

void I2CServer::HandleUpdate(const SystemStatus& current) {
  bool card_present = current.IsCardPresent();
  if (lastCardPresent != card_present) {
    updateFilenameCache = card_present;
    lastCardPresent = card_present;
    if (!card_present)
    {
      RequestCleanup(i2c_server_source_t::None);
    }
    else
    {
      RequestWiFiConnect(i2c_server_source_t::None);
    }
    if (isPresent)
    {
      const uint8_t sd_payload = card_present ? I2C_SERVER_SD_PRESENT : I2C_SERVER_SD_NOT_PRESENT;
      writeLengthPrefacedString(wire, I2C_SERVER_SD_STATUS_CHANGE, 1, (const char*)&sd_payload);
    }
  }

  status = current.ToJson();
  if (isSubscribed)
    writeLengthPrefacedString(wire, I2C_SERVER_SYSTEM_STATUS_JSON, status.length(), status.c_str());
}

static bool WireAvailableTimeout(TwoWire *wire, uint32_t timeout = 50)
{
  uint32_t start = millis();
  while((uint32_t)(millis() - start) < timeout)
  {
    if (wire->available())
      return true;
  }
  return false;
}

static uint16_t ReadInLength(TwoWire *wire) {
  // Read in the length.
  if (!WireAvailableTimeout(wire))
    return 0;
  int value = wire->read();
  uint16_t length = (value < 0) ? 0 : value;
  length = length << 8;
  if (!WireAvailableTimeout(wire))
    return 0;
  value = wire->read();
  length |= (value < 0) ? 0 : value;
  return length;
}

void I2CServer::UpgradeZuluControlFwRequest(UpgradeRequest request)
{
  uint8_t requestByte = (uint8_t)request;
  writeLengthPrefacedString(wire, I2C_SERVER_UPDATE_FW_REQUEST, 1, (const char*)&requestByte);
}

bool I2CServer::UpgradeZuluControlFwSendChunk(const uint8_t* chunk, size_t length, uint32_t* outSentCrc, absolute_time_t until)
{
  return I2CMasterDmaSendChunk(CLIENT_ADDR, I2C_SERVER_UPDATE_FW_DATA, chunk, (uint16_t)length, outSentCrc, until);
}

bool I2CServer::UpgradeZuluControlFwReadAck(uint16_t* outLength, uint32_t* outCrc, absolute_time_t until)
{
  while (true) {
    wire->requestFrom(CLIENT_ADDR, 3);
    if (WireAvailableTimeout(wire)) {
      uint8_t requestType = wire->read();
      if (requestType == I2C_CLIENT_UPDATE_FW_ACK) {
        uint16_t payloadLen = ReadInLength(wire);
        if (payloadLen == 6) {  // [len_hi][len_lo][crc32: 4 bytes]
          uint8_t buffer[6];
          bool ok = true;
          for (int pos = 0; pos < 6 && ok;) {
            int toRecv = (pos + BUFFER_LENGTH < 6) ? BUFFER_LENGTH : 6 - pos;
            wire->requestFrom(CLIENT_ADDR, toRecv);
            while (toRecv > 0) {
              if (!WireAvailableTimeout(wire)) { ok = false; break; }
              buffer[pos++] = wire->read();
              toRecv--;
            }
          }
          if (ok) {
            if (outLength) *outLength = ((uint16_t)buffer[0] << 8) | buffer[1];
            if (outCrc) *outCrc = ((uint32_t)buffer[2] << 24) | ((uint32_t)buffer[3] << 16) |
                                  ((uint32_t)buffer[4] << 8) | buffer[5];
            return true;
          }
        }
      }
      // Anything else (most commonly I2C_CLIENT_NOOP, the client's normal reply
      // when its core0 hasn't dequeued/processed the chunk and enqueued the ack
      // yet) just means "not ready" -- fall through and retry.
    }

    if (absolute_time_diff_us(get_absolute_time(), until) < 0) {
      return false;
    }
    sleep_us(200);
  }
}

void I2CServer::HandleImageResponse(const ImageResponse<i2c_server_source_t>& response)
{

  const std::unique_ptr<ImageResponse<i2c_server_source_t>> image_response  = std::make_unique<ImageResponse<i2c_server_source_t>>(response);
  switch (image_response->GetRequest().GetSource())
  {
    case i2c_server_source_t::FetchFilenames:
      HandleFetchFilenames(std::move(image_response));
      break;
    case i2c_server_source_t::FetchImages:
      HandleFetchImages(std::move(image_response));
      break;
    case i2c_server_source_t::FetchImage:
      HandleFetchImage(std::move(image_response));
      break;
    case i2c_server_source_t::SetToCurrent:
      HandleSetToCurrent(std::move(image_response));
      break;
    case i2c_server_source_t::None:
      // do nothing
      break;
  }
}

void I2CServer::HandleFetchFilenames(const std::unique_ptr<ImageResponse<i2c_server_source_t>>&& response)
{
  response_status_t status = response->GetStatus();
  static bool hit_end = false;
  if (hit_end == true || status == response_status_t::None)
  {
      EnterLoggingSafe();
      dbgmsg("End of fetch filenames");
      ExitLoggingSafe();
      hit_end = false;
      filenameTransferState = FilenameTransferState::Idle;
      auto emptyMsgBuf = "";
      writeLengthPrefacedString(wire, I2C_SERVER_IMAGE_FILENAME, 0, emptyMsgBuf);
      RequestCleanup(i2c_server_source_t::FetchFilenames);

  }
  else 
  {
    filenameTransferState = FilenameTransferState::Received;
    auto msgBuf = response->GetImage().GetFilename();
    writeLengthPrefacedString(wire, I2C_SERVER_IMAGE_FILENAME, msgBuf.size(), msgBuf.c_str());
    if (status == response_status_t::End)
    {
      hit_end = true;
    }
  }
}

void I2CServer::HandleFetchImages(const std::unique_ptr<ImageResponse<i2c_server_source_t>>&& response)
{
  static bool hit_end = false;
  response_status_t status = response->GetStatus();
  if (status == response_status_t::None || hit_end)
  {  
    if (hit_end)
    {
      EnterLoggingSafe();
      dbgmsg("End of fetch Images");
      ExitLoggingSafe();

    }
    hit_end = false;
    auto emptyMsgBuf = "";
    writeLengthPrefacedString(wire, I2C_SERVER_IMAGE_JSON, 0, emptyMsgBuf);
    RequestCleanup(i2c_server_source_t::FetchImages);
    sendFiles = false;
  }
  else
  {
    auto msgBuf = response->GetImage().ToJson();
    writeLengthPrefacedString(wire, I2C_SERVER_IMAGE_JSON, msgBuf.size(), msgBuf.c_str());

    hit_end = status == response_status_t::End;

    ImageRequest<i2c_server_source_t> next(image_request_t::Next, i2c_server_source_t::FetchImages);
    imageRequestPipe->RequestImageSafe(next);
  }
}

void I2CServer::HandleFetchImage(const std::unique_ptr<ImageResponse<i2c_server_source_t>>&& response)
{
    static bool hit_end = false;
    response_status_t status = response->GetStatus();
  if (status == response_status_t::None || hit_end)
  {
    if (hit_end)
    {
      EnterLoggingSafe();
      dbgmsg("End of Fetch image");
      ExitLoggingSafe();
    }
    hit_end = false;
    auto msgBuf = "";
    writeLengthPrefacedString(wire, I2C_SERVER_IMAGE_JSON, 0, msgBuf);
    isIterating = false;
    RequestCleanup(i2c_server_source_t::FetchImage);
  }
  else
  {
    auto msgBuf = response->GetImage().ToJson();
    writeLengthPrefacedString(wire, I2C_SERVER_IMAGE_JSON, msgBuf.size(), msgBuf.c_str());
  }
}

void I2CServer::HandleSetToCurrent(const std::unique_ptr<ImageResponse<i2c_server_source_t>>&& response)
{
  if (response->GetStatus() != response_status_t::None)
    deviceControl->LoadImageSafe(response->GetImage());
  RequestCleanup(i2c_server_source_t::SetToCurrent);
}

void I2CServer::RequestWiFiConnect(const i2c_server_source_t source)
{
      ImageRequest<i2c_server_source_t> connect(image_request_t::WiFiConnect, source);
      imageRequestPipe->RequestImageSafe(connect);
}


void I2CServer::RequestCleanup(const i2c_server_source_t source)
{
      ImageRequest<i2c_server_source_t> cleanup(image_request_t::Cleanup, source);
      imageRequestPipe->RequestImageSafe(cleanup);
}

void I2CServer::RequestReset(const i2c_server_source_t source)
{
      ImageRequest<i2c_server_source_t> reset(image_request_t::Reset, source);
      imageRequestPipe->RequestImageSafe(reset);
}

void I2CServer::Poll() {
  if (!devControlSet || !isPresent) {
    return;
  }

  if (updateFilenameCache && isSubscribed) {
    updateFilenameCache = false;
    filenameTransferState = FilenameTransferState::Start;
    auto buf = "";
    writeLengthPrefacedString(wire, I2C_SERVER_UPDATE_FILENAME_CACHE, 0, buf);
  }

  if (isSubscribed)
  {
    if (filenameTransferState == FilenameTransferState::Start)
    {
      EnterLoggingSafe();
      dbgmsg("I2C Server: Beginning of fetch filenames");
      RequestReset(i2c_server_source_t::FetchFilenames);
      filenameTransferState = FilenameTransferState::Sending;
      ImageRequest<i2c_server_source_t> request(image_request_t::Next, i2c_server_source_t::FetchFilenames);
      imageRequestPipe->RequestImageSafe(request);
      ExitLoggingSafe();
    }
    else if (filenameTransferState == FilenameTransferState::Received)
    {
      EnterLoggingSafe();
      filenameTransferState = FilenameTransferState::Sending;
      ImageRequest<i2c_server_source_t> request(image_request_t::Next, i2c_server_source_t::FetchFilenames);
      imageRequestPipe->RequestImageSafe(request);
      ExitLoggingSafe();
    }

    if (sendFiles) {
      EnterLoggingSafe();
      dbgmsg("I2C Server: Beginning of Fetch Images");
      RequestReset(i2c_server_source_t::FetchImages);
      ImageRequest<i2c_server_source_t> request;
      request.SetType(image_request_t::Next);
      request.SetSource(i2c_server_source_t::FetchImages);
      imageRequestPipe->RequestImageSafe(request);
      ExitLoggingSafe();
    }

    if (sendNextImage) {
      EnterLoggingSafe();
      if (!isIterating) {
        dbgmsg("I2C Server: Beginning of Fetch Image");
        isIterating = true;
        RequestReset(i2c_server_source_t::FetchImage);
      }
      ImageRequest<i2c_server_source_t> request;
      request.SetType(image_request_t::Next);
      request.SetSource(i2c_server_source_t::FetchImage);
      imageRequestPipe->RequestImageSafe(request);    
      sendNextImage = false;
      ExitLoggingSafe();
    }
  }

  wire->requestFrom(CLIENT_ADDR, 3);
  uint8_t requestType = wire->read();

  switch (requestType) {
  case I2C_CLIENT_API_VERSION:
  {
    EnterLoggingSafe();
    uint16_t length = ReadInLength(wire);
    if (length > 0)
    {
      // Client is sending some data.
      char* buffer = new char[length + 1];
      memset(buffer, 0, length + 1);

      bool i2c_read_timeout = false;
      for (int pos = 0; pos < length && !i2c_read_timeout;)
      {
        int toRecv = pos + BUFFER_LENGTH < length ? BUFFER_LENGTH : length - pos;

        wire->requestFrom(CLIENT_ADDR, toRecv);
        while (toRecv > 0) {
          if (!WireAvailableTimeout(wire)) {
            i2c_read_timeout = true;
            break;
          }
#ifndef CONTROL_CROSS_CORE_QUEUE
          ide_protocol_poll();
#endif
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

      if (apiVersionSent)
      {
        // Expected reply to our CheckForDevice() send — consume it, no write-back needed.
        apiVersionSent = false;
      }
      else
      {
        // ZuluControl has reset and is requesting our version — write back to complete handshake.
        isSubscribed = false;
        updateFilenameCache = true;
        static const char ide_api_ver[] = I2C_API_VERSION " ZuluIDE";
        writeLengthPrefacedString(wire, I2C_SERVER_API_VERSION, strlen(ide_api_ver), ide_api_ver);
      }

      delete[] buffer;
    }
    ExitLoggingSafe();
    break;
  }

  case I2C_CLIENT_SUBSCRIBE_STATUS_JSON: {
    EnterLoggingSafe();
    if (ReadInLength(wire) != 0) {
      logmsg("Length was not 0 for subscribe request.");
    }

    logmsg("I2C Client subscribed to updates.");
    isSubscribed = true;

    // Push current SD card status so the client has it immediately on subscribe.
    {
      const uint8_t sd_payload = lastCardPresent ? I2C_SERVER_SD_PRESENT : I2C_SERVER_SD_NOT_PRESENT;
      writeLengthPrefacedString(wire, I2C_SERVER_SD_STATUS_CHANGE, 1, (const char*)&sd_payload);
    }
    // Send over the current status.
    writeLengthPrefacedString(wire, I2C_SERVER_SYSTEM_STATUS_JSON, status.length(), status.c_str());
    ExitLoggingSafe();
    break;
  }

  case I2C_CLIENT_LOAD_IMAGE: {
    uint16_t length = ReadInLength(wire);

    if (length > 0) {
      // Client is sending some data.
      char* buffer = new char[length + 1];
      memset(buffer, 0, length + 1);

      bool i2c_read_timeout = false;
      for (int pos = 0; pos < length && !i2c_read_timeout;) {
        int toRecv = pos + BUFFER_LENGTH < length ? BUFFER_LENGTH : length - pos;

        wire->requestFrom(CLIENT_ADDR, toRecv);
        while (toRecv > 0) {
          if (!WireAvailableTimeout(wire)) {
            i2c_read_timeout = true;
            break;
          }
#ifndef CONTROL_CROSS_CORE_QUEUE
          ide_protocol_poll();
#endif
          buffer[pos++] = wire->read();
          toRecv--;
        }
      }

      if (buffer[0] != '\0')
      {

        EnterLoggingSafe();

        RequestReset(i2c_server_source_t::SetToCurrent);
        ImageRequest<i2c_server_source_t> current(image_request_t::Current, i2c_server_source_t::SetToCurrent);
        current.SetCurrentFilename(std::make_unique<std::string>(buffer));
        logmsg("I2C Client requested the current image be set to: ", current.GetCurrentFilename().c_str());
        imageRequestPipe->RequestImageSafe(current);
        ExitLoggingSafe();
      }


      delete[] buffer;
    }

    break;
  }

  case I2C_CLIENT_EJECT_IMAGE: {

    if (ReadInLength(wire) != 0) {
      EnterLoggingSafe();
      logmsg("Length was not 0 for eject image request.");
      ExitLoggingSafe();
    }

    deviceControl->EjectImageSafe();
    break;
  }

  case I2C_CLIENT_FETCH_FILENAMES: {
    EnterLoggingSafe();
    if (ReadInLength(wire) != 0) {
      logmsg("Length was not 0 for fetch filenames request.");
    }
    
    dbgmsg("I2C Client is fetching filenames");
    ExitLoggingSafe();
    sendFilenames = true;
    break;
  }
  
  case I2C_CLIENT_FETCH_IMAGES_JSON: {
    EnterLoggingSafe();
    if (ReadInLength(wire) != 0) {
      logmsg("Length was not 0 for fetch images request.");
    }
    
    dbgmsg("I2C Client is fetching images");
    ExitLoggingSafe();
    sendFiles = true;
    break;
  }

  case I2C_CLIENT_FETCH_ITR_IMAGE: {
    EnterLoggingSafe();
    if (ReadInLength(wire) != 0) {
      logmsg("Length was not 0 for fetch iterate image request.");
    }
    
    dbgmsg("I2C Client is fetching iterate image");
    ExitLoggingSafe();
    sendNextImage = true;
    
    break;
  }

  case I2C_CLIENT_FETCH_SSID: {
    EnterLoggingSafe();
    if (ReadInLength(wire) != 0) {
      logmsg("Length was not 0 for fetch ssid request.");
    }
    ExitLoggingSafe();
    writeLengthPrefacedString(wire, I2C_SERVER_SSID, ssid.length(), ssid.c_str());

    // static IP strings have a two letter prefix or empty if never they have never been set
    if (ip.length() > 2 && netmask.length() > 2 && gateway.length() > 2)
    {
        writeLengthPrefacedString(wire, I2C_SERVER_STATIC_IP, ip.length(), ip.c_str());
        writeLengthPrefacedString(wire, I2C_SERVER_STATIC_IP, gateway.length(), gateway.c_str());
        writeLengthPrefacedString(wire, I2C_SERVER_STATIC_IP, netmask.length(), netmask.c_str());
    }
    else
    {
        // clear static IP values on client and use DHCP
        writeLengthPrefacedString(wire, I2C_SERVER_STATIC_IP, 0, nullptr);
    }

    break;
  }

  case I2C_CLIENT_FETCH_SSID_PASS: {
    EnterLoggingSafe();
    if (ReadInLength(wire) != 0) {
      logmsg("Length was not 0 for fetch ssid pass request.");
    }
    ExitLoggingSafe();
    writeLengthPrefacedString(wire, I2C_SERVER_SSID_PASS, password.length(), password.c_str());
    WiFiConnect();
    break;
  }

  case I2C_CLIENT_NOOP: {
    uint16_t length = ReadInLength(wire);
    break;
  }

  case I2C_CLIENT_IP_ADDRESS: {
    uint16_t length = ReadInLength(wire);

    if (length > 0) {
      // Client is sending some data.
      char* buffer = new char[length + 1];
      memset(buffer, 0, length + 1);

      bool i2c_read_timeout = false;
      for (int pos = 0; pos < length && !i2c_read_timeout;) {
        int toRecv = ((BUFFER_LENGTH < length - pos) ? BUFFER_LENGTH : length - pos);

        wire->requestFrom(CLIENT_ADDR, toRecv);
        while (toRecv > 0) {
          if (!WireAvailableTimeout(wire)) {
            i2c_read_timeout = true;
            break;
          }
#ifndef CONTROL_CROSS_CORE_QUEUE
          ide_protocol_poll();
#endif
          buffer[pos++] = wire->read();
          toRecv--;
        }
      }
      EnterLoggingSafe();
      logmsg("I2C Client IP address is: ", buffer);
      ExitLoggingSafe();

      delete[] buffer;
      // Acknowledge IP received
      writeLengthPrefacedString(wire, I2C_SERVER_IP_ADDRESS_ACK, 0, "");
    }
    break;
  }

  case I2C_CLIENT_LOG_MSG: {
    uint16_t payload_length = ReadInLength(wire); 
    EnterLoggingSafe();
    if ( payload_length > 0) {
      // Client is sending some data.
      char* buffer = new char[payload_length+1];
      memset(buffer, '\0', payload_length+1);

      bool i2c_read_timeout = false;
      for (int pos = 0; pos < payload_length && !i2c_read_timeout;) {
        int toRecv = (BUFFER_LENGTH < payload_length - pos) ? BUFFER_LENGTH : payload_length - pos;

        wire->requestFrom(CLIENT_ADDR, toRecv);
        while (toRecv > 0) {
          if (!WireAvailableTimeout(wire)) {
            i2c_read_timeout = true;
            break;
          }
#ifndef CONTROL_CROSS_CORE_QUEUE
          ide_protocol_poll();
#endif
          buffer[pos] = wire->read();
          pos++;
          toRecv--;
        }
      }
      switch (buffer[0])
      {
        case ClientMessage::Prefix::Normal:
          logmsg("[I2C]: ", &buffer[1]);
          break;
        case ClientMessage::Prefix::Debug:
          dbgmsg("[I2C]: ", &buffer[1]);
          break;
        default:
          logmsg("[I2C]: ", buffer);
      }
      delete[] buffer;
    }
    else
    {
      // Legacy behaviour for I2C_CLIENT_NET_DOWN
      logmsg("I2C Client network is down.");
    }
    ExitLoggingSafe();
    break;
  }

  default: {
    break;
  }
  }

  imageResponsePipe->ProcessUpdates();
}

void I2CServer::SetSSID(const std::string& value) {
  ssid = value;
}

void I2CServer::SetPassword(const std::string &value) {
  password = value;
}

void I2CServer::SetIPv4(const std::string &value)
{
  ip = value;
}

void I2CServer::SetNetmask(const std::string &value)
{
  netmask = value;
}

void I2CServer::SetGateway(const std::string &value)
{
  gateway = value;
}

bool I2CServer::WiFiSSIDSet() {
  return !ssid.empty();
}

bool I2CServer::WiFiPasswordSet() {
  return !password.empty();
}

bool I2CServer::WiFiConnect()
{
  auto buf = "";
  return writeLengthPrefacedString(wire, I2C_SERVER_WIFI_CONNECT, 0, buf);
}

void I2CServer::UpdateFilenames() {
  EnterLoggingSafe();
  logmsg("Sending request to client to update the filenames");
  ExitLoggingSafe();
  auto emptyMsgBuf = "";
  writeLengthPrefacedString(wire, I2C_SERVER_UPDATE_FILENAME_CACHE, 0, emptyMsgBuf);
}

void I2CServer::EnterLoggingSafe()
{
#ifdef CONTROL_CROSS_CORE_QUEUE
  mutex_enter_blocking(platform_get_log_mutex());
#endif
}

void I2CServer::ExitLoggingSafe()
{
#ifdef CONTROL_CROSS_CORE_QUEUE
  mutex_exit(platform_get_log_mutex());
#endif
}