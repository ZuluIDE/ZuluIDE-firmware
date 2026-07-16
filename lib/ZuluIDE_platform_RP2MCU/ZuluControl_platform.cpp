/**
 * ZuluIDE™ - Copyright (c) 2026 Rabbit Hole Computing™
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

#include "ZuluIDE_platform.h"
#include "ZuluIDE_log.h"
#include "ZuluIDE_config.h"
#include <ZuluIDE.h>
#include <SdFat.h>
#include <assert.h>
#include <minIni.h>
#include "display/display_ssd1306.h"
#include "rotary_control.h"
#include <zuluide/i2c/i2c_server.h>
#include <zuluide/i2c/i2c_server_src_type.h>
#include <zuluide/pipe/image_response.h>
#include <zuluide/control/select_controller_src_type.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <pico/time.h>
#include "ZuluControl_platform.h"

#define CONTROLLER_TYPE_BOARD 1
#define CONTROLLER_TYPE_WIFI  2

zuluide::control::RotaryControl g_rotary_input;
static TwoWire g_wire(GPIO_I2C_DEVICE, GPIO_I2C_SDA, GPIO_I2C_SCL);
uint32_t g_i2c_bus_speed = I2C_FAST_BUS_SPEED;
zuluide::DisplaySSD1306 display;

zuluide::pipe::ImageResponsePipe<zuluide::control::select_controller_source_t>* g_controllerImageResponsePipe;
zuluide::pipe::ImageResponsePipe<zuluide::i2c::i2c_server_source_t> g_I2CServerImageResponsePipe;
zuluide::pipe::ImageRequestPipe<zuluide::i2c::i2c_server_source_t> g_I2CServerImageRequestPipe;
zuluide::i2c::I2CServer g_I2cServer(&g_I2CServerImageRequestPipe, &g_I2CServerImageResponsePipe);
zuluide::ObserverTransfer<zuluide::status::SystemStatus> *uiStatusController;

// Force this definition to defined somewhere else
// bootloader linker was linking to this function from this discarded object in the bootloader linker script
extern template void logmsg(const char*, const char*);


static void processStatusUpdate(const zuluide::status::SystemStatus &currentStatus) {
    // Notify the hardware UI of updates.
    display.HandleUpdate(currentStatus);

    // Notify the I2C server of updates.
     g_I2cServer.HandleUpdate(currentStatus);
}

static void recover_i2c_bus()
{
    // Temporarily take manual control of SCL/SDA to clock out any device
    // that got stuck mid-transaction (e.g. due to an unexpected reset).
    gpio_set_function(GPIO_I2C_SCL, GPIO_FUNC_SIO);
    gpio_set_function(GPIO_I2C_SDA, GPIO_FUNC_SIO);
    gpio_pull_up(GPIO_I2C_SCL);
    gpio_pull_up(GPIO_I2C_SDA);
    gpio_set_dir(GPIO_I2C_SCL, false);  // SCL: input to read current line state
    if (!gpio_get(GPIO_I2C_SCL))
    {
        // A slave is clock-stretching (holding SCL low). Wait for it to release
        // before we attempt to drive SCL ourselves.
        for (int i = 0; i < 200 && !gpio_get(GPIO_I2C_SCL); i++)
        {
            busy_wait_us_32(1000);
        }

    }
    gpio_set_dir(GPIO_I2C_SCL, true);  // SCL: output
    gpio_set_dir(GPIO_I2C_SDA, false);  // SDA: input
    gpio_put(GPIO_I2C_SCL, 1);

    // Toggle SCL up to 16 times until SDA is released (HIGH)
    for (int i = 0; i < 16; i++)
    {
        gpio_put(GPIO_I2C_SCL, 0);
        busy_wait_us_32(5);
        gpio_put(GPIO_I2C_SCL, 1);
        busy_wait_us_32(5);
        if (gpio_get(GPIO_I2C_SDA))
        {
            break;
        }
    }

    // Issue a STOP condition: SDA low → SCL high → SDA high
    gpio_set_dir(GPIO_I2C_SDA, true);
    gpio_put(GPIO_I2C_SCL, 0);
    busy_wait_us_32(5);
    gpio_put(GPIO_I2C_SDA, 0);
    busy_wait_us_32(5);
    gpio_put(GPIO_I2C_SCL, 1);
    busy_wait_us_32(5);
    gpio_put(GPIO_I2C_SDA, 1);
    busy_wait_us_32(5);
    // Caller is responsible for re-initializing or closing the I2C peripheral.
}

uint8_t platform_check_for_controller()
{
    static bool checked = false;
    static uint8_t controller_found = 0;
    if (checked) return controller_found;
    g_i2c_bus_speed = I2C_FAST_BUS_SPEED;
    g_wire.setClock(g_i2c_bus_speed);  // 400 kHz (Fast Mode)
    // Setting the drive strength seems to help the I2C bus with the ZuluControl and the controller OLED display
    // to communicate and handshake properly
    gpio_set_drive_strength(GPIO_I2C_SCL, GPIO_DRIVE_STRENGTH_12MA);
    gpio_set_drive_strength(GPIO_I2C_SDA, GPIO_DRIVE_STRENGTH_12MA);

    g_rotary_input.SetI2c(&g_wire);
    bool hasHardwareUI = g_rotary_input.CheckForDevice();
    if (!hasHardwareUI)
    {
        g_i2c_bus_speed = I2C_NORMAL_BUS_SPEED;
        g_wire.setClock(g_i2c_bus_speed);
        hasHardwareUI = g_rotary_input.CheckForDevice();
        if (hasHardwareUI)
        {
            logmsg("I2C bus successfully fell back to normal bus speed (100kHz)");
        }
        else
        {
            // set back to high speed for other device checks
            g_i2c_bus_speed = I2C_FAST_BUS_SPEED;
            g_wire.setClock(g_i2c_bus_speed);
        }
    }
    // RotaryControl::CheckForDevice() sets its own (much shorter, non-recovering) timeout
    // on this shared Wire instance internally. Reassert ours here, after it runs and before
    // anything I2C-server-related (including a firmware upgrade) touches the bus, so it
    // doesn't get silently overridden.
    //
    // Short per-transaction timeout so a single attempt that happens to land during the
    // client's brief (tens-of-ms) flash-write busy window fails fast and gets retried by
    // the caller's own poll loop, instead of camping on one attempt for seconds and eating
    // most of the overall ack-wait budget (and repeatedly bus-recovering a client that may
    // just be about to come back on its own).
    g_wire.setTimeout(500, true);
    g_I2cServer.SetI2c(&g_wire);
    g_I2cServer.SetI2cRaw(GPIO_I2C_DEVICE);
    bool hasI2CServer = g_I2cServer.CheckForDevice();
    logmsg(hasHardwareUI ? "Hardware UI found." : "Hardware UI not found.");
    logmsg(hasI2CServer ? "I2C server found" : "I2C server not found");

    if (hasI2CServer)
    {
        FsFile root = SD.open("/");
        FsFile file;
        while (file.openNext(&root))
        {
            char filename[MAX_FILE_PATH];
            file.getName(filename, sizeof(filename));
            const char *extension = strrchr(filename, '.');
            if (extension 
                && strncasecmp(extension, ".uf2", 4) == 0
                && strncasecmp(filename, ZULUCONTROL_UF2_PREFIX, sizeof(ZULUCONTROL_UF2_PREFIX) - 1) == 0)
            {
                file.close();
                logmsg("ZuluControl-firmware UF2, \"", filename,"\", found on SD card, attempting to upgrade firmware");
                platform_i2c_upgrade_zulucontrol_fw(filename);
                break;
            }
        }
        file.close();
        root.close();

        if (!ini_haskey("UI", "wifissid", CONFIGFILE))
        {
            logmsg("-- Warning: I2C server detected but no WiFi SSID configured.");
            logmsg("-- Set with \"wifissid\" under \"[UI]\" section of ", CONFIGFILE, " and insert SD card");
        }

        g_I2CServerImageRequestPipe.Reset();
        g_I2CServerImageResponsePipe.Reset();
        g_I2CServerImageRequestPipe.AddObserver([&](zuluide::pipe::ImageRequest<zuluide::i2c::i2c_server_source_t> t){g_I2CServerImageResponsePipe.HandleRequest(t);});
    }
    controller_found = (hasHardwareUI ? CONTROLLER_TYPE_BOARD : 0) | (hasI2CServer ? CONTROLLER_TYPE_WIFI : 0);
    checked = true;
    return controller_found;
}

void platform_set_status_controller(zuluide::ObserverTransfer<zuluide::status::SystemStatus> *statusController) {
    logmsg("Initialized platform controller with the status controller.");
    display.init(&g_wire);
    statusController->AddObserver(processStatusUpdate);
    uiStatusController = statusController;
}

void platform_set_controller_image_response_pipe(zuluide::pipe::ImageResponsePipe<zuluide::control::select_controller_source_t> *imageRequestPipe) {
    logmsg("Initialized platform with filename request pipe");
    g_controllerImageResponsePipe = imageRequestPipe;
}

void platform_set_display_controller(zuluide::Observable<zuluide::control::DisplayState>& displayController) {
    logmsg("Initialized platform controller with the display controller.");
    displayController.AddObserver([&] (auto current) -> void {display.HandleUpdate(current);}); 
}

void platform_set_input_interface(zuluide::control::InputReceiver* inputReceiver) {
    logmsg("Initialized platform controller with input receiver.");
    g_rotary_input.SetReceiver(inputReceiver);
    g_rotary_input.StartSendingEvents();
}

void platform_set_device_control(zuluide::status::DeviceControlSafe* deviceControl) {
    g_I2cServer.SetDeviceControl(deviceControl);
 }


void platform_close_i2c()
{
    // Release any clock-stretching slave and issue a STOP before closing,
    // so the slave is not left mid-transaction after the MCU reboots.
    recover_i2c_bus();
    g_wire.end();
}

void platform_wifi_controller_connect()
{
    logmsg("Initializing platform with WiFi device control.");
    if (!g_I2cServer.CheckForDevice())
        logmsg("-- WiFi Device not responding");

    char iniBuffer[100];
    memset(&iniBuffer, 0, sizeof(iniBuffer));
    if (ini_gets("UI", "wifissid", "", iniBuffer, sizeof(iniBuffer), CONFIGFILE) > 0) {
        auto ssid = std::string(iniBuffer);
        g_I2cServer.SetSSID(ssid);
        logmsg("-- Set SSID from INI file to ", ssid.c_str());
        
        bool fallBackToDHCP = false;
        bool usingStaticIP = (ini_haskey("UI","wifi_static_ip", CONFIGFILE)
            && ini_haskey("UI", "wifi_static_netmask", CONFIGFILE)
            && ini_haskey("UI", "wifi_static_gateway", CONFIGFILE));
        std::string empty = "";
        g_I2cServer.SetIPv4(empty);
        g_I2cServer.SetNetmask(empty);
        g_I2cServer.SetGateway(empty);
        if (usingStaticIP)
        {
            logmsg("-- Using static IP settings:");
            // prefix string with data types
            memset(&iniBuffer, 0, sizeof(iniBuffer));
            stpcpy(iniBuffer, "ip");
            ini_gets("UI", "wifi_static_ip", "", &iniBuffer[2], sizeof(iniBuffer) - 2, CONFIGFILE);
            auto ip = std::string(iniBuffer);
            g_I2cServer.SetIPv4(ip);
            logmsg("---- IP Address: ", ip.length() > 2 ? &iniBuffer[2] : "missing");

            memset(&iniBuffer, 0, sizeof(iniBuffer));
            stpcpy(iniBuffer, "nm");
            ini_gets("UI", "wifi_static_netmask", "", &iniBuffer[2], sizeof(iniBuffer) - 2, CONFIGFILE);
            auto netmask = std::string(iniBuffer);
            g_I2cServer.SetNetmask(netmask);
            logmsg("---- Netmask:    ", netmask.length() > 2 ? &iniBuffer[2] : "missing");

            memset(&iniBuffer, 0, sizeof(iniBuffer));
            stpcpy(iniBuffer, "gw");
            ini_gets("UI", "wifi_static_gateway", "", &iniBuffer[2], sizeof(iniBuffer) - 2, CONFIGFILE);
            auto gateway = std::string(iniBuffer);
            g_I2cServer.SetGateway(gateway);
            logmsg("---- Gateway:    ", gateway.length() > 2 ? &iniBuffer[2] : "missing");
            fallBackToDHCP = (ip.length() <= 2 || netmask.length() <= 2 || gateway.length() <= 2);
        }
        if (usingStaticIP)
        {
            if (fallBackToDHCP)
            {
                logmsg("-- Falling back do DHCP due to missing static IP setting");
            }
        }
        else
        {
            logmsg("-- Using DHCP");
        }

        memset(&iniBuffer, 0, 100);
        std::string wifiPass = "";
        if (ini_gets("UI", "wifipassword", "", iniBuffer, sizeof(iniBuffer), CONFIGFILE) > 0) {
            wifiPass = std::string(iniBuffer);
            logmsg("-- Set password from ", CONFIGFILE," file, using WiFi authentication.");
        } else {
            logmsg("-- No or empty password set from ", CONFIGFILE," file, assuming an open WiFi network.");
        }
        g_I2cServer.SetPassword(wifiPass);

    }
    if (!g_I2cServer.WiFiSSIDSet()) {
        // The I2C server responded but we cannot configure wifi. This is an invalid configuration which must be communicated, so it can be corrected.
        logmsg("WARNING: An I2C client was detected, but no Wi-Fi SSID/password settings are configured in zuluide.ini");
        logmsg("These missing ini file parameters are required to successfully connect the i2c server via Wi-Fi");
        logmsg("Visit https://www.ZuluIDE.com/manual for further information.");
    }
}

void platform_i2c_upgrade_zulucontrol_fw(const char* filename)
{
    g_I2cServer.UpgradeZuluControlFwRequest(zuluide::i2c::I2CServer::UpgradeRequest::START);

    // Fast LED blink, one toggle per chunk sent
    LED_OFF();

    // Open the firmware file and send it in chunks to the I2C client
    FsFile fwFile = SD.open(filename, O_RDONLY);
    if (!fwFile) {
        logmsg("Failed to open firmware file: ", filename);
        g_I2cServer.UpgradeZuluControlFwRequest(zuluide::i2c::I2CServer::UpgradeRequest::ABORT);
        LED_OFF();
        return;
    }

    const size_t chunkSize = I2C_UPGRADE_MAX_CHUNK;
    // Static, not stack-local: this function is called deep in the boot call stack
    // (platform_check_for_controller -> setup), and a buffer this size is worth
    // being defensive about even though it's unlikely to blow a typical
    // RP2040/2350 stack.
    static uint8_t buffer[chunkSize];
    uint32_t totalBytesSent = 0;
    uint32_t chunksSent = 0;
    uint32_t lastProgressLogTime = millis();
    while (fwFile.isOpen()) {
        int bytesRead = fwFile.read(buffer, chunkSize);
        if (bytesRead == 0)
        {
            // Clean EOF. When the file size is an exact multiple of chunkSize,
            // the *previous* iteration's read already consumed a full final
            // chunk (bytesRead == chunkSize), so the "bytesRead < chunkSize"
            // early-out below never fires for it -- EOF is only observable on
            // this following read, which correctly returns 0. That's a normal
            // way to finish, not an error.
            logmsg("-- Finished sending ZuluControl-firmware file: ", filename, " (", (int)fwFile.size(), " bytes)");
            break;
        }
        if (bytesRead < 0)
        {
            g_I2cServer.UpgradeZuluControlFwRequest(zuluide::i2c::I2CServer::UpgradeRequest::ABORT);
            fwFile.close();
            LED_OFF();
            logmsg("-- Error reading ZuluControl-firmware file: ", filename);
            return;
        }

        // Two distinct failure modes get one retry counter: a transaction-level
        // failure (NACK/timeout) just resends -- nothing was staged client-side.
        // A content mismatch (CRC or length wrong on an otherwise-successful round
        // trip) sends RETRY first, since that's the only case where the client
        // actually has something staged that needs discarding.
        bool chunk_ok = false;
        for (int attempt = 0; attempt < 3 && !chunk_ok; attempt++) {
            if (attempt > 0) {
                sleep_ms(2);
            }

            uint32_t sent_crc = 0;
            bool sent = g_I2cServer.UpgradeZuluControlFwSendChunk(
                buffer, bytesRead, &sent_crc, make_timeout_time_ms(2000));
            if (!sent) {
                logmsg("-- Failed to send firmware chunk at offset: ", (int)totalBytesSent,
                       " (attempt ", (int)(attempt + 1), "/3)");
                continue;
            }

            uint16_t ack_len = 0;
            uint32_t ack_crc = 0;
            bool acked = g_I2cServer.UpgradeZuluControlFwReadAck(
                &ack_len, &ack_crc, make_timeout_time_ms(2000));
            if (!acked) {
                logmsg("-- I2C client did not acknowledge the firmware chunk at offset: ",
                       (int)totalBytesSent, " (attempt ", (int)(attempt + 1), "/3)");
                continue;
            }

            if (ack_len != (uint16_t)bytesRead || ack_crc != sent_crc) {
                logmsg("-- Error: I2C client ack mismatch at offset: ", (int)totalBytesSent,
                       " expected len=", (int)bytesRead, " crc=", (int)sent_crc,
                       " got len=", (int)ack_len, " crc=", (int)ack_crc,
                       " (attempt ", (int)(attempt + 1), "/3)");
                g_I2cServer.UpgradeZuluControlFwRequest(zuluide::i2c::I2CServer::UpgradeRequest::RETRY);
                continue;
            }

            chunk_ok = true;
        }

        if (!chunk_ok) {
            logmsg("-- Error: firmware chunk failed after 3 attempts at offset: ", (int)totalBytesSent);
            g_I2cServer.UpgradeZuluControlFwRequest(zuluide::i2c::I2CServer::UpgradeRequest::ABORT);
            fwFile.close();
            LED_OFF();
            return;
        }

        totalBytesSent += bytesRead;
        chunksSent++;
        if (chunksSent & 1) LED_ON(); else LED_OFF();

        // Log progress at most every 2 seconds rather than every chunk --
        // otherwise a large file at 2048 bytes/chunk floods the log with a
        // line every few milliseconds.
        uint32_t now = millis();
        if ((uint32_t)(now - lastProgressLogTime) >= 2000) {
            lastProgressLogTime = now;
            uint32_t percent = (uint32_t)((uint64_t)totalBytesSent * 100 / fwFile.size());
            logmsg("-- Bytes of UF2 file sent to device: ", (int)totalBytesSent, "/",
                   (int)fwFile.size(), " bytes total (", (int)percent, "%)");
        }

        if (bytesRead < (int)chunkSize) {
            logmsg("-- Finished sending ZuluControl-firmware file: ", filename, " (", (int)fwFile.size(), " bytes)");
            break;
        }

        platform_reset_watchdog();
    }

    fwFile.close();
    g_I2cServer.UpgradeZuluControlFwRequest(zuluide::i2c::I2CServer::UpgradeRequest::FINISH);
    LED_OFF();

    // The client sends a sentinel ack (length 0xFFFF, crc32 0x00000000 -- a
    // value no real chunk ack can ever produce, since chunks are capped at
    // I2C_UPGRADE_MAX_CHUNK bytes) once it has successfully committed the
    // final chunk to flash and is about to reboot into the new firmware.
    // Only delete the upgrade file once that's actually been confirmed: if
    // the ack never arrives (e.g. it was lost, or the client failed to
    // finish), the file needs to stay put so the upgrade can be retried on
    // the next boot.
    uint16_t finishAckLen = 0;
    uint32_t finishAckCrc = 0;
    bool finishAcked = g_I2cServer.UpgradeZuluControlFwReadAck(
        &finishAckLen, &finishAckCrc, make_timeout_time_ms(3000));
    if (finishAcked && finishAckLen == I2C_CLIENT_ACK_SPECIAL_LEN && finishAckCrc == I2C_CLIENT_ACK_CRC_UPGRADE_COMPLETE) {
        logmsg("-- ZuluControl-firmware update confirmed, connected device is rebooting. Removing file: ", filename);
        SD.remove(filename);
        g_I2cServer.ForceIsPresent();
    } else {
        logmsg("-- Warning: did not receive confirmation that the ZuluControl-firmware update "
               "succeeded; leaving ", filename, " in place for retry");
    }
}