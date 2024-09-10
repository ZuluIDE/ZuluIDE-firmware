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
#include <SerialUSB.h>
#include <class/cdc/cdc_device.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include "ZuluIDE_platform_gpio.h"
#include "display_ssd1306.h"
#include "rotary_control.h"
#include <zuluide/i2c/i2c_server.h>
#include <minIni.h>

#ifdef ENABLE_AUDIO_OUTPUT
#  include "audio.h"
#endif // ENABLE_AUDIO_OUTPUT

const char *g_platform_name = PLATFORM_NAME;
static uint32_t g_flash_chip_size = 0;
static bool g_uart_initialized = false;
static bool g_led_disabled = false;
static bool g_led_blinking = false;
static bool g_dip_drive_id, g_dip_cable_sel;
static uint64_t g_flash_unique_id;
static zuluide::control::RotaryControl g_rotary_input;
static TwoWire g_wire(i2c1, GPIO_I2C_SDA, GPIO_I2C_SCL);
static zuluide::DisplaySSD1306 display;
static queue_t g_status_update_queue;
static uint8_t g_eject_buttons = 0;
static zuluide::i2c::I2CServer g_I2cServer;
static mutex_t logMutex;

//void mbed_error_hook(const mbed_error_ctx * error_context);

/***************/
/* GPIO init   */
/***************/

// Helper function to configure whole GPIO in one line
static void gpio_conf(uint gpio, enum gpio_function fn, bool pullup, bool pulldown, bool output, bool initial_state, bool fast_slew)
{
    gpio_put(gpio, initial_state);
    gpio_set_dir(gpio, output);
    gpio_set_pulls(gpio, pullup, pulldown);
    gpio_set_function(gpio, fn);

    if (fast_slew)
    {
        padsbank0_hw->io[gpio] |= PADS_BANK0_GPIO0_SLEWFAST_BITS;
    }
}


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

void platform_init()
{
    // Make sure second core is stopped
    multicore_reset_core1();

    /* Check dip switch settings */
    gpio_conf(DIP_CABLESEL,     GPIO_FUNC_SIO, false, false, false, false, false);
    gpio_conf(DIP_DRIVE_ID,     GPIO_FUNC_SIO, false, false, false, false, false);
    gpio_conf(DIP_DBGLOG,       GPIO_FUNC_SIO, false, false, false, false, false);

    delay(10); // 10 ms delay to let pull-ups do their work
    mutex_init(&logMutex);

    bool dbglog = !gpio_get(DIP_DBGLOG);
    g_dip_cable_sel = !gpio_get(DIP_CABLESEL);
    g_dip_drive_id = !gpio_get(DIP_DRIVE_ID);

    /* Initialize logging to SWO pin (UART0) */
    gpio_conf(SWO_PIN,        GPIO_FUNC_UART,false,false, true,  false, true);
    uart_init(uart0, 1000000); // Debug UART at 1 MHz baudrate
    g_uart_initialized = true;

    logmsg("Platform: ", g_platform_name);
    logmsg("FW Version: ", g_log_firmwareversion);

    logmsg("DIP switch settings: cablesel ", (int)g_dip_cable_sel, ", drive_id ", (int)g_dip_drive_id, " debug log ", (int)dbglog);

    g_log_debug = dbglog;
    
#ifdef ENABLE_AUDIO_OUTPUT
    logmsg("SP/DIF audio to expansion header enabled");
    logmsg("-- Overclocking to 135.428571MHz");
    reclock_for_audio();
#endif

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

    // SD card pins
    // Card is used in SDIO mode, rp2040_sdio.cpp will redirect these to PIO1
    //        pin             function       pup   pdown  out    state fast
    gpio_conf(SDIO_CLK,       GPIO_FUNC_SIO, true, false, true,  true, true);
    gpio_conf(SDIO_CMD,       GPIO_FUNC_SIO, true, false, false, true, true);
    gpio_conf(SDIO_D0,        GPIO_FUNC_SIO, true, false, false, true, true);
    gpio_conf(SDIO_D1,        GPIO_FUNC_SIO, true, false, false, true, true);
    gpio_conf(SDIO_D2,        GPIO_FUNC_SIO, true, false, false, true, true);
    gpio_conf(SDIO_D3,        GPIO_FUNC_SIO, true, false, false, true, true);

    // I2C pins
    //        pin                 function       pup   pdown  out    state fast
    gpio_conf(GPIO_I2C_SCL,       GPIO_FUNC_I2C, true, false, false,  true, true);
    gpio_conf(GPIO_I2C_SDA,       GPIO_FUNC_I2C, true, false, false,  true, true);

    // FPGA bus
    // Signals will be switched between SPI/PIO by rp2040_fpga.cpp, but pull-ups are configured here.
    //        pin             function       pup   pdown  out    state fast
    gpio_conf(FPGA_CLK,       GPIO_FUNC_GPCK,false,false, true,  false, true);
    gpio_conf(FPGA_CRESET,    GPIO_FUNC_SIO, false,false, true,  false, false);
    gpio_conf(FPGA_CDONE,     GPIO_FUNC_SIO, true, false, false, false, false);
    gpio_conf(FPGA_SS,        GPIO_FUNC_SIO, true, false, true,  true,  false);
    gpio_conf(FPGA_QSPI_SCK,  GPIO_FUNC_SIO, false,false, true,  false, true);
    gpio_conf(FPGA_QSPI_D0,   GPIO_FUNC_SIO, true, false, true,  false, true);
    gpio_conf(FPGA_QSPI_D1,   GPIO_FUNC_SIO, true, false, true,  false, true);
    gpio_conf(FPGA_QSPI_D2,   GPIO_FUNC_SIO, true, false, true,  false, true);
    gpio_conf(FPGA_QSPI_D3,   GPIO_FUNC_SIO, true, false, true,  false, true);

    // IDE initialization status signals
    gpio_conf(IDE_CSEL_IN,    GPIO_FUNC_SIO, false,false, false, false, false);
    gpio_conf(IDE_PDIAG_IN,   GPIO_FUNC_SIO, false,false, false, false, false);
    gpio_conf(IDE_DASP_IN,    GPIO_FUNC_SIO, false,false, false, false, false);

    // Status LED
    gpio_conf(STATUS_LED,     GPIO_FUNC_SIO, false,false, true,  false, false);
}

// late_init() only runs in main application
void platform_late_init()
{
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
    // one-time control setup for DMA channels and second core
    audio_setup();
#endif

}

bool platform_check_for_controller()
{
  g_rotary_input.SetI2c(&g_wire);
  bool hasHardwareUI = g_rotary_input.CheckForDevice();
  bool hasI2CServer = g_I2cServer.CheckForDevice();
  logmsg(hasHardwareUI ? "Hardware UI found." : "Hardware UI not found.");
  logmsg(hasI2CServer ? "I2C server Found" : "I2C server not found");
  return hasHardwareUI || hasI2CServer;
}

void platform_set_status_controller(zuluide::ObservableSafe<zuluide::status::SystemStatus>& statusController) {
  logmsg("Initialized platform controller with the status controller.");
  display.init(&g_wire);
  queue_init(&g_status_update_queue, sizeof(zuluide::status::SystemStatus*), 5);
  statusController.AddObserver(&g_status_update_queue);
}

void platform_set_display_controller(zuluide::Observable<zuluide::control::DisplayState>& displayController) {
  logmsg("Initialized platform controller with the display controller.");
  displayController.AddObserver([&] (auto current) -> void {display.HandleUpdate(current);});  
}

void platform_set_input_interface(zuluide::control::InputReceiver* inputReceiver) {
  logmsg("Inialized platform controller with input receiver.");
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
  
  g_I2cServer.Init(&g_wire, deviceControl);
}

void platform_poll_input() {
  g_rotary_input.Poll();
}

void platform_write_led(bool state)
{
    if (g_led_disabled || g_led_blinking) return;

    gpio_put(STATUS_LED, state);
}

void platform_set_blink_status(bool status)
{
    g_led_blinking = status;
}

void platform_write_led_override(bool state)
{
    if (g_led_disabled) return;

    gpio_put(STATUS_LED, state);

}
void platform_disable_led(void)
{   
    g_led_disabled = true;
    logmsg("Disabling status LED");
}

void platform_init_eject_button(uint8_t eject_button)
{
    if (eject_button & 1)
    {
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

int platform_get_device_id(void)
{
    if (g_dip_cable_sel)
    {
        if (gpio_get(IDE_CSEL_IN))
            return 1;    // CSEL wire has been cut, secondary device
        else
            return 0;    // CSEL wire grounded, primary device
    }
    else
    {
        if (g_dip_drive_id)
            return 1;    // PRI/SEC switch on, secondary device
        else
            return 0;    // PRI/SEC switch off, primary device
    }
}

/*****************************************/
/* Crash handlers                        */
/*****************************************/

extern SdFs SD;
extern uint32_t __StackTop;
static void usb_log_poll();

void platform_emergency_log_save()
{
    platform_set_sd_callback(NULL, NULL);

    SD.begin(SD_CONFIG_CRASH);
    FsFile crashfile = SD.open(CRASHFILE, O_WRONLY | O_CREAT | O_TRUNC);

    if (!crashfile.isOpen())
    {
        // Try to reinitialize
        int max_retry = 10;
        while (max_retry-- > 0 && !SD.begin(SD_CONFIG_CRASH));

        crashfile = SD.open(CRASHFILE, O_WRONLY | O_CREAT | O_TRUNC);
    }

    uint32_t startpos = 0;
    crashfile.write(log_get_buffer(&startpos));
    crashfile.write(log_get_buffer(&startpos));
    crashfile.flush();
    crashfile.close();
}

extern "C" __attribute__((noinline))
void show_hardfault(uint32_t *sp)
{
    uint32_t pc = sp[6];
    uint32_t lr = sp[5];

    logmsg("--------------");
    logmsg("CRASH!");
    logmsg("Platform: ", g_platform_name);
    logmsg("FW Version: ", g_log_firmwareversion);
    logmsg("SP: ", (uint32_t)sp);
    logmsg("PC: ", pc);
    logmsg("LR: ", lr);
    logmsg("R0: ", sp[0]);
    logmsg("R1: ", sp[1]);
    logmsg("R2: ", sp[2]);
    logmsg("R3: ", sp[3]);

    uint32_t *p = (uint32_t*)((uint32_t)sp & ~3);

    for (int i = 0; i < 8; i++)
    {
        if (p == &__StackTop) break; // End of stack

        logmsg("STACK ", (uint32_t)p, ":    ", p[0], " ", p[1], " ", p[2], " ", p[3]);
        p += 4;
    }

    platform_emergency_log_save();

    while (1)
    {
        usb_log_poll();
        // Flash the crash address on the LED
        // Short pulse means 0, long pulse means 1
        platform_set_blink_status(false);
        int base_delay = 500;
        for (int i = 31; i >= 0; i--)
        {
            LED_OFF();
            for (int j = 0; j < base_delay; j++) busy_wait_ms(1);

            int delay = (pc & (1 << i)) ? (3 * base_delay) : base_delay;
            LED_ON();
            for (int j = 0; j < delay; j++) busy_wait_ms(1);
            LED_OFF();
        }

        for (int j = 0; j < base_delay * 10; j++) busy_wait_ms(1);
    }
}

extern "C" __attribute__((naked, interrupt))
void isr_hardfault(void)
{
    // Copies stack pointer into first argument
    asm("mrs r0, msp\n"
        "bl show_hardfault": : : "r0");
}

/*****************************************/
/* Debug logging and watchdog            */
/*****************************************/

// Send log data to USB UART if USB is connected.
// Data is retrieved from the shared log ring buffer and
// this function sends as much as fits in USB CDC buffer.
//
// This is normally called by platform_reset_watchdog() in
// the normal polling loop. If code hangs, the watchdog_callback()
// also starts calling this after 2 seconds.
// This ensures that log messages get passed even if code hangs,
// but does not unnecessarily delay normal execution.
static void usb_log_poll()
{
    static uint32_t logpos = 0;

    if (Serial.availableForWrite())
    {
        // Retrieve pointer to log start and determine number of bytes available.
        uint32_t available = 0;
        const char *data = log_get_buffer(&logpos, &available);
                // Limit to CDC packet size
        uint32_t len = available;
        if (len == 0) return;
        if (len > CFG_TUD_CDC_EP_BUFSIZE) len = CFG_TUD_CDC_EP_BUFSIZE;
        
        // Update log position by the actual number of bytes sent
        // If USB CDC buffer is full, this may be 0
        uint32_t actual = 0;
        actual = Serial.write(data, len);
        logpos -= available - actual;
    }
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

// This function is called for every log message.
void platform_log(const char *s)
{
    if (g_uart_initialized)
    {
        uart_puts(uart0, s);
    }
}

static int g_watchdog_timeout;
static bool g_watchdog_initialized;
static bool g_watchdog_did_bus_reset;

void ide_phy_reset_from_watchdog();

static void watchdog_callback(unsigned alarm_num)
{
    g_watchdog_timeout -= 1000;

    if (g_watchdog_timeout < WATCHDOG_CRASH_TIMEOUT - 1000)
    {
        // Been stuck for at least a second, start dumping USB log
        usb_log_poll();
    }

    if (g_watchdog_timeout <= WATCHDOG_CRASH_TIMEOUT - WATCHDOG_BUS_RESET_TIMEOUT)
    {
        if (!g_watchdog_did_bus_reset)
        {
            logmsg("--------------");
            logmsg("WATCHDOG TIMEOUT, attempting bus reset");
            logmsg("GPIO states: out ", sio_hw->gpio_out, " oe ", sio_hw->gpio_oe, " in ", sio_hw->gpio_in);

            uint32_t *p = (uint32_t*)__get_MSP();
            for (int i = 0; i < 16; i++)
            {
                if (p == &__StackTop) break; // End of stack

                logmsg("STACK ", (uint32_t)p, ":    ", p[0], " ", p[1], " ", p[2], " ", p[3]);
                p += 4;
            }

            g_watchdog_did_bus_reset = true;
            ide_phy_reset_from_watchdog();
        }

        if (g_watchdog_timeout <= 0)
        {
            logmsg("--------------");
            logmsg("WATCHDOG TIMEOUT!");
            logmsg("Platform: ", g_platform_name);
            logmsg("FW Version: ", g_log_firmwareversion);
            logmsg("GPIO states: out ", sio_hw->gpio_out, " oe ", sio_hw->gpio_oe, " in ", sio_hw->gpio_in);

            uint32_t *p = (uint32_t*)__get_MSP();
            for (int i = 0; i < 16; i++)
            {
                if (p == &__StackTop) break; // End of stack

                logmsg("STACK ", (uint32_t)p, ":    ", p[0], " ", p[1], " ", p[2], " ", p[3]);
                p += 4;
            }

            fpga_dump_ide_regs();

            usb_log_poll();
            platform_emergency_log_save();

#ifndef RP2040_DISABLE_BOOTLOADER
            platform_boot_to_main_firmware();
#else
            NVIC_SystemReset();
#endif
        }
    }

    hardware_alarm_set_target(alarm_num, delayed_by_ms(get_absolute_time(), 1000));
}

// This function can be used to periodically reset watchdog timer for crash handling.
// It can also be left empty if the platform does not use a watchdog timer.
void platform_reset_watchdog()
{
    g_watchdog_timeout = WATCHDOG_CRASH_TIMEOUT;
    g_watchdog_did_bus_reset = false;
    if (!g_watchdog_initialized)
    {
        int alarm_num = -1;
        for (int i = 0; i < NUM_TIMERS; i++)
        {
            if (!hardware_alarm_is_claimed(i))
            {
                alarm_num = i;
                break;
            }
        }
        if (alarm_num == -1)
        {
            logmsg("No free watchdog hardware alarms to claim");
            return;
        }
        hardware_alarm_claim(alarm_num);
        hardware_alarm_set_callback(alarm_num, &watchdog_callback);
        hardware_alarm_set_target(alarm_num, delayed_by_ms(get_absolute_time(), 1000));
        g_watchdog_initialized = true;
    }

    // USB log is polled here also to make sure any log messages in fault states
    // get passed to USB.
    usb_log_poll();
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

    if (status < 0x80 || status > 0x84)
    {
        logmsg("---- New license key is not valid for this device, not installing (status ", status, ")");
        return false;
    }
    else
    {
        logmsg("---- New license key accepted, writing to flash (status ", status, ")");
    }

    usb_log_poll();

    // Write to RP2040 flash
    __disable_irq();
    flash_range_erase(PLATFORM_LICENSE_KEY_OFFSET, PLATFORM_FLASH_PAGE_SIZE);
    flash_range_program(PLATFORM_LICENSE_KEY_OFFSET, key, 256);
    __enable_irq();

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

/*****************************************/
/* Flash reprogramming from bootloader   */
/*****************************************/

#ifdef PLATFORM_BOOTLOADER_SIZE

extern uint32_t __real_vectors_start;
extern uint32_t __StackTop;
static volatile void *g_bootloader_exit_req;

__attribute__((section(".time_critical.platform_rewrite_flash_page")))
bool platform_rewrite_flash_page(uint32_t offset, uint8_t buffer[PLATFORM_FLASH_PAGE_SIZE])
{
    if (offset == PLATFORM_BOOTLOADER_SIZE)
    {
        if (buffer[3] != 0x20 || buffer[7] != 0x10)
        {
            logmsg("Invalid firmware file, starts with: ", bytearray(buffer, 16));
            return false;
        }
    }

    if (NVIC_GetEnableIRQ(USBCTRL_IRQ_IRQn))
    {
        logmsg("Disabling USB during firmware flashing");
        NVIC_DisableIRQ(USBCTRL_IRQ_IRQn);
        usb_hw->main_ctrl = 0;
    }

    dbgmsg("Writing flash at offset ", offset, " data ", bytearray(buffer, 4));
    assert(offset % PLATFORM_FLASH_PAGE_SIZE == 0);
    assert(offset >= PLATFORM_BOOTLOADER_SIZE);

    // Avoid any mbed timer interrupts triggering during the flashing.
    __disable_irq();

    // For some reason any code executed after flashing crashes
    // unless we disable the XIP cache.
    // Not sure why this happens, as flash_range_program() is flushing
    // the cache correctly.
    // The cache is now enabled from bootloader start until it starts
    // flashing, and again after reset to main firmware.
    xip_ctrl_hw->ctrl = 0;

    flash_range_erase(offset, PLATFORM_FLASH_PAGE_SIZE);
    flash_range_program(offset, buffer, PLATFORM_FLASH_PAGE_SIZE);

    uint32_t *buf32 = (uint32_t*)buffer;
    uint32_t num_words = PLATFORM_FLASH_PAGE_SIZE / 4;
    for (int i = 0; i < num_words; i++)
    {
        uint32_t expected = buf32[i];
        uint32_t actual = *(volatile uint32_t*)(XIP_NOCACHE_BASE + offset + i * 4);

        if (actual != expected)
        {
            logmsg("Flash verify failed at offset ", offset + i * 4, " got ", actual, " expected ", expected);
            return false;
        }
    }

    __enable_irq();

    return true;
}

void platform_boot_to_main_firmware()
{
    // To ensure that the system state is reset properly, we perform
    // a SYSRESETREQ and jump straight from the reset vector to main application.
    g_bootloader_exit_req = &g_bootloader_exit_req;
    SCB->AIRCR = 0x05FA0004;
    while(1);
}

void btldr_reset_handler()
{
    uint32_t* application_base = &__real_vectors_start;
    if (g_bootloader_exit_req == &g_bootloader_exit_req)
    {
        // Boot to main application
        application_base = (uint32_t*)(XIP_BASE + PLATFORM_BOOTLOADER_SIZE);
    }

    SCB->VTOR = (uint32_t)application_base;
    __asm__(
        "msr msp, %0\n\t"
        "bx %1" : : "r" (application_base[0]),
                    "r" (application_base[1]) : "memory");
}

// Replace the reset handler when building the bootloader
// The rp2040_btldr.ld places real vector table at an offset.
__attribute__((section(".btldr_vectors")))
const void * btldr_vectors[2] = {&__StackTop, (void*)&btldr_reset_handler};

#endif


/********************************/
/* 2nd core code                */
/********************************/
void zuluide_setup(void)
{
//   if (!platform_check_for_controller())
//   {
//     rp2040.idleOtherCore();
//     multicore_reset_core1();
//     dbgmsg("No Zulu Control board or I2C server found, disabling 2nd core");
//   }
}

void zuluide_setup1(void)
{

}

void zuluide_main_loop1(void)
{
    platform_poll_input();

    // Look for device status updates.
    zuluide::status::SystemStatus *currentStatus;
    if (queue_try_remove(&g_status_update_queue, &currentStatus)) {
      // Notify the hardware UI of updates.
      display.HandleUpdate(*currentStatus);
      
      // Notify the I2C server of updates.
      g_I2cServer.HandleUpdate(*currentStatus);
      delete(currentStatus);
    } else {
      // Only need to check refresh if there wasn't an update.
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

mutex_t* platform_get_log_mutex() {
  return &logMutex;
}
