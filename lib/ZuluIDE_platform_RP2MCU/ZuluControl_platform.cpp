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
#include "ZuluControl_platform.h"

#define CONTROLLER_TYPE_BOARD 1
#define CONTROLLER_TYPE_WIFI  2

zuluide::control::RotaryControl g_rotary_input;
static TwoWire g_wire(GPIO_I2C_DEVICE, GPIO_I2C_SDA, GPIO_I2C_SCL);
zuluide::DisplaySSD1306 display;

zuluide::pipe::ImageResponsePipe<zuluide::control::select_controller_source_t>* g_controllerImageResponsePipe;
zuluide::pipe::ImageResponsePipe<zuluide::i2c::i2c_server_source_t> g_I2CServerImageResponsePipe;
zuluide::pipe::ImageRequestPipe<zuluide::i2c::i2c_server_source_t> g_I2CServerImageRequestPipe;
zuluide::i2c::I2CServer g_I2cServer(&g_I2CServerImageRequestPipe, &g_I2CServerImageResponsePipe);
zuluide::ObserverTransfer<zuluide::status::SystemStatus> *uiStatusController;


static void processStatusUpdate(const zuluide::status::SystemStatus &currentStatus) {
    // Notify the hardware UI of updates.
    display.HandleUpdate(currentStatus);

    // Notify the I2C server of updates.
     g_I2cServer.HandleUpdate(currentStatus);
}

uint8_t platform_check_for_controller()
{
    static bool checked = false;
    static uint8_t controller_found = 0;
    if (checked) return controller_found;
    g_wire.setClock(100000);
    // Setting the drive strength seems to help the I2C bus with the Pico W controller and the controller OLED display
    // to communicate and handshake properly
    gpio_set_drive_strength(GPIO_I2C_SCL, GPIO_DRIVE_STRENGTH_12MA);
    gpio_set_drive_strength(GPIO_I2C_SDA, GPIO_DRIVE_STRENGTH_12MA);
    
    g_rotary_input.SetI2c(&g_wire);
    bool hasHardwareUI = g_rotary_input.CheckForDevice();
    g_I2cServer.SetI2c(&g_wire);
    bool hasI2CServer = g_I2cServer.CheckForDevice();
    logmsg(hasHardwareUI ? "Hardware UI found." : "Hardware UI not found.");
    logmsg(hasI2CServer ? "I2C server found" : "I2C server not found");
    if (hasI2CServer && !ini_haskey("UI", "wifissid", CONFIGFILE))
    {
        logmsg("-- Warning: I2C server detected but no WiFi SSID configured.");
        logmsg("-- Set with \"wifissid\" under \"[UI]\" section of ", CONFIGFILE, " and insert SD card");
    }

    if(hasI2CServer)
    {
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
        // The I2C server responded but we cannot configure wifi. This may cause issues.
        logmsg("-- An I2C client was detected WIFI SSID is not configured. This will cause problems if the I2C client needs WIFI configuration data.");
    }
}