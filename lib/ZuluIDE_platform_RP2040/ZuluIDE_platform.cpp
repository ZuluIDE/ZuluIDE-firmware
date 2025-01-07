/**
 * ZuluIDE™ - Copyright (c) 2023 Rabbit Hole Computing™
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

#include "ZuluIDE_platform.h"
#include "ZuluIDE_log.h"
#include "ZuluIDE_config.h"
#include <ZuluIDE.h>
#include "ide_phy.h"
#include <SdFat.h>
#include <assert.h>
#include <hardware/gpio.h>
#include <hardware/adc.h>
#include <hardware/uart.h>
#include <hardware/spi.h>
#include <hardware/pll.h>
#include <hardware/structs/xip_ctrl.h>
#include <hardware/structs/usb.h>
#include <hardware/structs/iobank0.h>
#include <hardware/flash.h>
#include <pico/multicore.h>
#include "rp2040_fpga.h"
#include <strings.h>
#include <RP2040USB.h>
#include <SerialUSB.h>
#include <class/cdc/cdc_device.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include "ZuluIDE_platform_gpio.h"
#include "display/display_ssd1306.h"
#include "rotary_control.h"
#include <zuluide/i2c/i2c_server.h>
#include <minIni.h>

#ifdef ENABLE_AUDIO_OUTPUT
#  include "audio.h"
#endif // ENABLE_AUDIO_OUTPUT

#define CONTROLLER_TYPE_BOARD 1
#define CONTROLLER_TYPE_WIFI  2

static uint32_t g_flash_chip_size = 0;
static uint64_t g_flash_unique_id;
static zuluide::control::RotaryControl g_rotary_input;
static TwoWire g_wire(i2c1, GPIO_I2C_SDA, GPIO_I2C_SCL);
static zuluide::DisplaySSD1306 display;
static uint8_t g_eject_buttons = 0;
static zuluide::i2c::I2CServer g_I2cServer;
static zuluide::ObserverTransfer<zuluide::status::SystemStatus> *uiStatusController;

void processStatusUpdate(const zuluide::status::SystemStatus &update);

extern void usb_log_poll();

//void mbed_error_hook(const mbed_error_ctx * error_context);

/***************/
/* GPIO init   */
/***************/

#ifdef ENABLE_AUDIO_OUTPUT
// Increases clk_sys and clk_peri to 135.428571MHz at runtime to support
// division to audio output rates. Invoke before anything is using clk_peri
// except for the logging UART, which is handled below.
static void reclock_for_audio() {
    // ensure UART is fully drained before we mess up its clock
    uart_tx_wait_blocking(uart0);
    // switch clk_sys and clk_peri to pll_usb
    // see code in 2.15.6.1 of the datasheet for useful comments
    clock_configure(clk_sys,
            CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLKSRC_CLK_SYS_AUX,
            CLOCKS_CLK_SYS_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB,
            48 * MHZ,
            48 * MHZ);
    clock_configure(clk_peri,
            0,
            CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB,
            48 * MHZ,
            48 * MHZ);
    // reset PLL for 135.428571MHz
    pll_init(pll_sys, 1, 948000000, 7, 1);
    // switch clocks back to pll_sys
    clock_configure(clk_sys,
            CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLKSRC_CLK_SYS_AUX,
            CLOCKS_CLK_SYS_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
            135428571,
            135428571);
    clock_configure(clk_peri,
            0,
            CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
            135428571,
            135428571);
    // reset UART for the new clock speed
    uart_init(uart0, 1000000);
}


#endif

static void reclock_to_default()
{
    // ensure UART is fully drained before we mess up its clock
    uart_tx_wait_blocking(uart0);
    // switch clk_sys and clk_peri to pll_usb
    // see code in 2.15.6.1 of the datasheet for useful comments
    clock_configure(clk_sys,
            CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLKSRC_CLK_SYS_AUX,
            CLOCKS_CLK_SYS_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB,
            48 * MHZ,
            48 * MHZ);
    clock_configure(clk_peri,
            0,
            CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB,
            48 * MHZ,
            48 * MHZ);
    // reset PLL for 135.428571MHz
    pll_init(pll_sys, 1, 1500000000, 6, 2);
    // switch clocks back to pll_sys
    clock_configure(clk_sys,
            CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLKSRC_CLK_SYS_AUX,
            CLOCKS_CLK_SYS_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
            125000000,
            125000000);
    clock_configure(clk_peri,
            0,
            CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
            125000000,
            125000000);
    // reset UART for the new clock speed
    uart_init(uart0, 1000000);

}

// late_init() only runs in main application
void platform_late_init()
{
    // Get flash chip size
    uint8_t cmd_read_jedec_id[4] = {0x9f, 0, 0, 0};
    uint8_t response_jedec[4] = {0};
    flash_do_cmd(cmd_read_jedec_id, response_jedec, 4);
    g_flash_chip_size = (1 << response_jedec[3]);
    logmsg("Flash chip size: ", (int)(g_flash_chip_size / 1024), " kB");

    // Get flash chip unique ID
    // (flash_get_unique_id() from RP2040 libs didn't work for some reason)
    uint8_t cmd_read_uniq_id[13] = {0x4B};
    uint8_t response_uniq_id[13] = {0};
    flash_do_cmd(cmd_read_uniq_id, response_uniq_id, 13);
    memcpy(&g_flash_unique_id, response_uniq_id + 5, 8);
    logmsg("Flash unique ID: ", g_flash_unique_id);

    dbgmsg("Loading FPGA bitstream");
    if (fpga_init())
    {
        logmsg("FPGA initialization succeeded");
    }
    else
    {
        logmsg("ERROR: FPGA initialization failed");
    }
#ifdef ENABLE_AUDIO_OUTPUT
    logmsg("I2S audio to expansion header enabled");
    reclock_for_audio();
    logmsg("-- System clock is set to ", (int) clock_get_hz(clk_sys),  "Hz");
#endif
#ifdef ENABLE_AUDIO_OUTPUT
    // one-time control setup for DMA channels and second core
    audio_init();
#endif
    platform_check_for_controller();
    __USBStart();
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
  controller_found = (hasHardwareUI ? CONTROLLER_TYPE_BOARD : 0) | (hasI2CServer ? CONTROLLER_TYPE_WIFI : 0);
  checked = true;
  return controller_found;
}

void platform_set_status_controller(zuluide::ObserverTransfer<zuluide::status::SystemStatus> *statusController) {
  logmsg("Initialized platform controller with the status controller.");
  display.init(&g_wire);
  uiStatusController = statusController;
  uiStatusController->AddObserver(processStatusUpdate);
}

void platform_set_display_controller(zuluide::Observable<zuluide::control::DisplayState>& displayController) {
  logmsg("Initialized platform controller with the display controller.");
  displayController.AddObserver([&] (auto current) -> void {display.HandleUpdate(current);});  
}

void platform_set_input_interface(zuluide::control::InputReceiver* inputReceiver) {
  logmsg("Initialized platform controller with input receiver.");
  g_rotary_input.SetReciever(inputReceiver);
  g_rotary_input.StartSendingEvents();
}

void platform_set_device_control(zuluide::status::DeviceControlSafe* deviceControl) {
  logmsg("Initialized platform with device control.");
  char iniBuffer[100];
  memset(&iniBuffer, 0, 100);
  if (ini_gets("UI", "wifissid", "", iniBuffer, sizeof(iniBuffer), CONFIGFILE) > 0) {
    auto ssid = std::string(iniBuffer);
    g_I2cServer.SetSSID(ssid);
    logmsg("Set SSID from INI file to ", ssid.c_str());
  }

  memset(&iniBuffer, 0, 100);
  if (ini_gets("UI", "wifipassword", "", iniBuffer, sizeof(iniBuffer), CONFIGFILE) > 0) {
    auto wifiPass = std::string(iniBuffer);
    g_I2cServer.SetPassword(wifiPass);
    logmsg("Set PASSWORD from INI file.");
  }

  if ((platform_check_for_controller() & CONTROLLER_TYPE_WIFI) && !g_I2cServer.WifiCredentialsSet()) {
    // The I2C server responded but we cannot configure wifi. This may cause issues.
    logmsg("An I2C client was detected but the WIFI credentials are not configured. This will cause problems if the I2C client needs WIFI configuration data.");
  }

  g_I2cServer.SetDeviceControl(deviceControl);
}

void platform_poll_input() {
  g_rotary_input.Poll();
}

void platform_init_eject_button(uint8_t eject_button)
{
    if (eject_button & 1)
    {
        //        pin                   function       pup   pdown  out    state fast
        gpio_conf(GPIO_EJECT_BTN_1_PIN, GPIO_FUNC_SIO, true, false, false, true, false);
        g_eject_buttons |= 1;
    }

    if (eject_button & 2)
    {
        gpio_conf(GPIO_EJECT_BTN_2_PIN, GPIO_FUNC_SIO, true, false, false, true, false);
        g_eject_buttons |= 2;
    }
}

uint8_t platform_get_buttons()
{
    uint8_t buttons = 0;

    if ((g_eject_buttons & 1) && (!gpio_get(GPIO_EJECT_BTN_1_PIN))) 
        buttons |= 1;
    if ((g_eject_buttons & 2) && !gpio_get(GPIO_EJECT_BTN_2_PIN)) 
        buttons |= 2;

    // Simple debouncing logic: handle button releases after 100 ms delay.
    static uint32_t debounce;
    static uint8_t buttons_debounced = 0;

    if (buttons != 0)
    {
        buttons_debounced = buttons;
        debounce = millis();
    }
    else if ((uint32_t)(millis() - debounce) > 100)
    {
        buttons_debounced = 0;
    }

    return buttons_debounced;
}


// Use ADC to implement supply voltage monitoring for the +3.0V rail.
// This works by sampling the temperature sensor channel, which has
// a voltage of 0.7 V, allowing to calculate the VDD voltage.
static void adc_poll()
{
#if PLATFORM_VDD_WARNING_LIMIT_mV > 0
    static bool initialized = false;
    static int lowest_vdd_seen = PLATFORM_VDD_WARNING_LIMIT_mV;

    if (!initialized)
    {
        adc_init();
        adc_set_temp_sensor_enabled(true);
        adc_set_clkdiv(65535); // Lowest samplerate, about 2 kHz
        adc_select_input(4);
        adc_fifo_setup(true, false, 0, false, false);
        adc_run(true);
        initialized = true;
    }

#ifdef ENABLE_AUDIO_OUTPUT
    /*
    * If ADC sample reads are done, either via direct reading, FIFO, or DMA,
    * at the same time a SPI DMA write begins, it appears that the first
    * 16-bit word of the DMA data is lost. This causes the bitstream to glitch
    * and audio to 'pop' noticably. For now, just disable ADC reads when audio
    * is playing.
    */
   if (audio_is_active()) return;
#endif  // ENABLE_AUDIO_OUTPUT

    int adc_value_max = 0;
    while (!adc_fifo_is_empty())
    {
        int adc_value = adc_fifo_get();
        if (adc_value > adc_value_max) adc_value_max = adc_value;
    }

    // adc_value = 700mV * 4096 / Vdd
    // => Vdd = 700mV * 4096 / adc_value
    // To avoid wasting time on division, compare against
    // limit directly.
    const int limit = (700 * 4096) / PLATFORM_VDD_WARNING_LIMIT_mV;
    if (adc_value_max > limit)
    {
        // Warn once, and then again if we detect even a lower drop.
        int vdd_mV = (700 * 4096) / adc_value_max;
        if (vdd_mV < lowest_vdd_seen)
        {
            logmsg("WARNING: Detected supply voltage drop to ", vdd_mV, "mV. Verify power supply is adequate.");
            lowest_vdd_seen = vdd_mV - 50; // Small hysteresis to avoid excessive warnings
        }
    }
#endif
}


// Install FPGA license key to RP2040 flash
// buf is pointer to hex string with 26 bytes (encoding 13 bytes)
bool install_license(char *buf)
{
    uint8_t key[256] = {0};
    for (int i = 0; i < 13; i++)
    {
        char tmp[3] = {buf[i * 2], buf[i * 2 + 1], 0};
        key[i] = strtoull(tmp, NULL, 16);
    }

    if (memcmp(key, PLATFORM_LICENSE_KEY_ADDR, 32) == 0)
    {
        logmsg("---- License key matches the one already installed");
        return true;
    }


    reclock_to_default();
    // Make a test run with the license key and wait for FPGA to validate it
    logmsg("---- Testing new license key..");
    fpga_init(true, false);
    fpga_wrcmd(FPGA_CMD_LICENSE_AUTH, key, 32);
    for (int i = 0; i < 20; i++)
    {
        usb_log_poll();
        delay(100);
    }

    // Check validation results
    uint8_t status;
    fpga_rdcmd(FPGA_CMD_LICENSE_CHECK, &status, 1);

#ifdef ENABLE_AUDIO_OUTPUT
    reclock_for_audio();
#endif

    if (status < 0x80 || status > 0x84)
    {
        logmsg("---- New license key is not valid for this device, not installing (status ", status, ")");
        return false;
    }
    else
    {
        logmsg("---- New license key accepted, writing to flash (status ", status, ")");
    }


    // Write to RP2040 flash
    uint32_t saved_irq = save_and_disable_interrupts();
    flash_range_erase(PLATFORM_LICENSE_KEY_OFFSET, PLATFORM_FLASH_PAGE_SIZE);
    flash_range_program(PLATFORM_LICENSE_KEY_OFFSET, key, 256);
    restore_interrupts(saved_irq);

    if (memcmp(key, PLATFORM_LICENSE_KEY_ADDR, 32) == 0)
    {
        logmsg("---- Flash write successful");
        return true;
    }
    else
    {
        logmsg("---- Flash compare failed: ", bytearray(key, 5), " vs. ", bytearray(PLATFORM_LICENSE_KEY_ADDR, 5));
        return false;
    }
}

void usb_command_handler(char *cmd)
{
    if (strncasecmp(cmd, "license ", 8) == 0)
    {
        logmsg("-- Installing new license key received from USB port");
        char *p = cmd + 8;
        while (isspace(*p)) p++;

        if (strlen(p) < 26)
        {
            logmsg("---- License key too short: ", p);
        }
        else
        {
            install_license(p);
        }
    }
}

// Poll for commands sent through the USB serial port
void usb_command_poll()
{
    static uint8_t rx_buf[64];
    static int rx_len;

    uint32_t available = Serial.available();
    if (available > 0)
    {
        available = std::min<uint32_t>(available, sizeof(rx_buf) - rx_len);
        Serial.readBytes(rx_buf + rx_len, available);
        rx_len += available;
    }

    if (rx_len > 0)
    {
        char *first = (char*)rx_buf;
        for (int i = 0; i < rx_len; i++)
        {
            if (rx_buf[i] == '\n' || rx_buf[i] == '\r')
            {
                // Got complete line
                rx_buf[i] = '\0';
                usb_command_handler(first);
                rx_len = 0;
            }
            else if (isspace(*first))
            {
                first++;
            }
        }

        if (rx_len == sizeof(rx_buf))
        {
            // Too long line, discard
            rx_len = 0;
        }
    }
}

// Poll function that is called every few milliseconds.
// Can be left empty or used for platform-specific processing.
void platform_poll()
{
    static uint32_t prev_poll_time;
    static bool license_log_done = false;
    static bool license_from_sd_done = false;

    // No point polling the USB hardware more often than once per millisecond
    uint32_t time_now = millis();
    if (time_now == prev_poll_time)
    {
        return;
    }
    prev_poll_time = time_now;

    // Check if there is license file on SD card
    if (!license_from_sd_done && g_sdcard_present)
    {
        license_from_sd_done = true;

        if (SD.exists(LICENSEFILE))
        {
            char buf[26];
            FsFile f = SD.open(LICENSEFILE, O_RDONLY);
            if (f.read(buf, 26) == 26)
            {
                logmsg("-- Found license key file ", LICENSEFILE);
                install_license(buf);
            }
            f.close();
            SD.remove(LICENSEFILE);
        }
    }

    // Log FPGA license status after initial delay from boot
    if (!license_log_done && time_now >= 2000)
    {
        uint8_t response[21];
        fpga_rdcmd(FPGA_CMD_LICENSE_CHECK, response, 21);
        logmsg("FPGA license request code: ",
            bytearray((uint8_t*)&g_flash_unique_id, 8),
            bytearray(response + 1, 4),
            bytearray(response + 16, 5));

        if (response[0] == 0 || response[0] == 0xFF)
        {
            logmsg("-------------------------------------------------");
            logmsg("ERROR: FPGA license check failed with status ", response[0]);
            logmsg("       Please contact customer support and provide this log file and proof of purchase.");
            logmsg("-------------------------------------------------");
        }
        else
        {
            logmsg("FPGA license accepted with status ", response[0]);
        }

        license_log_done = true;
    }

    // Monitor supply voltage and process USB events
    adc_poll();
    usb_log_poll();
    usb_command_poll();

#ifdef ENABLE_AUDIO_OUTPUT
    audio_poll();
#endif // ENABLE_AUDIO_OUTPUT
}



/********************************/
/* 2nd core code                */
/********************************/
void zuluide_setup(void)
{
   if (!platform_check_for_controller())
   {
     rp2040.idleOtherCore();
     multicore_reset_core1();
     dbgmsg("No Zulu Control board or I2C server found, disabling 2nd core");
   }
}

void zuluide_setup1(void)
{

}

/***
    Execution actions on the UI core.
**/
void zuluide_main_loop1(void)
{
    platform_poll_input();

    // Process status update, if any exist.
    if (!uiStatusController->ProcessUpdate()) {
      // If no updates happend, refresh the display (enables animation)
      display.Refresh();
    }

    g_I2cServer.Poll();
}


extern "C"
{
    void setup1(void)
    {
        zuluide_setup1();
    }
    void loop1(void)
    {
        zuluide_main_loop1();
    }
}

void processStatusUpdate(const zuluide::status::SystemStatus &currentStatus) {
  // Notify the hardware UI of updates.
  display.HandleUpdate(currentStatus);
  
  // Notify the I2C server of updates.
  g_I2cServer.HandleUpdate(currentStatus);
}
