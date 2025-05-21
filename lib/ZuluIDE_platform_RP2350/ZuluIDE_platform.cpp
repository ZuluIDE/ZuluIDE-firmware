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
#include <strings.h>
#include <SerialUSB.h>
#include <class/cdc/cdc_device.h>
#include <Wire.h>
// #include <Adafruit_SSD1306.h>
#include "ZuluIDE_platform_gpio.h"
#include "rp2350_sniffer.h"
#include <zuluide_rp2350b_core1.h>
#include <sdio_rp2350.h>
// #include "display/display_ssd1306.h"
// #include "rotary_control.h"
// #include <zuluide/i2c/i2c_server.h>
#include <minIni.h>

#ifdef ENABLE_AUDIO_OUTPUT
#  include "audio.h"
#endif // ENABLE_AUDIO_OUTPUT

#define CONTROLLER_TYPE_BOARD 1
#define CONTROLLER_TYPE_WIFI  2

const char *g_platform_name = PLATFORM_NAME;
static uint32_t g_flash_chip_size = 0;
static bool g_uart_initialized = false;
static bool g_led_disabled = false;
static bool g_led_blinking = false;
static bool g_dip_drive_id, g_dip_cable_sel, g_cable_sel_state;
static uint64_t g_flash_unique_id;
static mutex_t logMutex;
static uint8_t g_eject_buttons = 0;
static bool g_sniffer_enabled;

//void mbed_error_hook(const mbed_error_ctx * error_context);

/***************/
/* GPIO init   */
/***************/

// Helper function to configure whole GPIO in one line
static void gpio_conf(uint gpio, gpio_function_t fn, bool pullup, bool pulldown, bool output, bool initial_state, bool fast_slew)
{
    gpio_put(gpio, initial_state);
    gpio_set_dir(gpio, output);
    gpio_set_pulls(gpio, pullup, pulldown);
    gpio_set_function(gpio, fn);

    if (fast_slew)
    {
        gpio_set_slew_rate(gpio, GPIO_SLEW_RATE_FAST);
    }
    else
    {
        gpio_set_slew_rate(gpio, GPIO_SLEW_RATE_SLOW);
    }
}

void platform_minimal_init()
{
    // SD card pins
    // Card is used in SDIO mode, rp2040_sdio.cpp will redirect these to PIO1
    //        pin             function       pup   pdown  out    state fast
    gpio_conf(SDIO_CLK,       GPIO_FUNC_SIO, true, false, true,  true, true);
    gpio_conf(SDIO_CMD,       GPIO_FUNC_SIO, true, false, false, true, true);
    gpio_conf(SDIO_D0,        GPIO_FUNC_SIO, true, false, false, true, true);
    gpio_conf(SDIO_D1,        GPIO_FUNC_SIO, true, false, false, true, true);
    gpio_conf(SDIO_D2,        GPIO_FUNC_SIO, true, false, false, true, true);
    gpio_conf(SDIO_D3,        GPIO_FUNC_SIO, true, false, false, true, true);

    // Status LED
    gpio_conf(STATUS_LED,     GPIO_FUNC_SIO, false,false, true,  false, false);

    /* Initialize logging to SWO pin (UART0) */
    gpio_conf(SWO_PIN,        GPIO_FUNC_UART_AUX,false,false, true,  false, true);
    uart_init(uart0, 1000000); // Debug UART at 1 MHz baudrate
    g_uart_initialized = true;
}

void platform_init()
{
#ifdef ZULUIDE_RP2350B_CORE1_HAVE_SOURCE
    // Make sure second core is stopped
    multicore_reset_core1();
#endif

    /* Check dip switch settings */
    //        pin             function       pup   pdown  out    state fast
    gpio_conf(IDE_DATASEL,      GPIO_FUNC_SIO, false, false, true, true, true);
    gpio_conf(CTRL_IN_SEL,      GPIO_FUNC_SIO, false, false, true,  false, true);
    gpio_conf(DIP_CABLESEL,     GPIO_FUNC_SIO, false, false, false, false, false);
    gpio_conf(DIP_DRIVE_ID,     GPIO_FUNC_SIO, false, false, false, false, false);
    gpio_conf(DIP_DBGLOG,       GPIO_FUNC_SIO, false, false, false, false, false);
    gpio_conf(IDE_CABLESEL,     GPIO_FUNC_SIO, false, false, false, false, false);

    delay(10); // 10 ms delay to let pull-ups do their work
    mutex_init(&logMutex);

    bool dbglog = !gpio_get(DIP_DBGLOG);
    g_dip_cable_sel = !gpio_get(DIP_CABLESEL);
    g_dip_drive_id = !gpio_get(DIP_DRIVE_ID);
    g_cable_sel_state = gpio_get(IDE_CABLESEL);

    // Disable CTRL IN mux for now
    gpio_put(CTRL_IN_SEL, true);

    /* Initialize logging to SWO pin (UART0) */
    gpio_conf(SWO_PIN,        GPIO_FUNC_UART_AUX,false,false, true,  false, true);
    uart_init(uart0, 1000000); // Debug UART at 1 MHz baudrate
    g_uart_initialized = true;

    logmsg("Platform: ", g_platform_name);
    logmsg("FW Version: ", g_log_firmwareversion);

    logmsg("DIP switch settings: cablesel ", (int)g_dip_cable_sel, ", drive_id ", (int)g_dip_drive_id, " debug log ", (int)dbglog);

    g_log_debug = dbglog;

#ifndef ARM_NONSECURE_MODE
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
#endif

    logmsg("System clock is set to ", (int) clock_get_hz(clk_sys), " Hz");

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

    // IDE bus control signals
    //        pin             function       pup   pdown  out    state fast
    gpio_conf(CTRL_LOAD,      GPIO_FUNC_SIO, false, false, true,  false, true);
    gpio_conf(CTRL_nEN,       GPIO_FUNC_SIO, false, false, true,  true,  true);
    gpio_conf(CTRL_IN_SEL,    GPIO_FUNC_SIO, false, false, true,  true,  true);
    gpio_conf(IDE_DATASEL,    GPIO_FUNC_SIO, false, false, true,  true, true);
    gpio_conf(IDE_DATADIR,    GPIO_FUNC_SIO, false, false, true,  false, true);
    gpio_conf(IDE_IORDY_OUT,  GPIO_FUNC_SIO, false, false, true,  false, true);
    gpio_conf(IDE_IORDY_EN,   GPIO_FUNC_SIO, false, false, true,  true,  true);
    gpio_conf(IDE_IOCS16,     GPIO_FUNC_SIO, false, false, true,  true,  true);

    // IDE bus status signals
    gpio_conf(IDE_RST,        GPIO_FUNC_SIO, false, false, false, false, true);
    gpio_conf(IDE_DIOW,       GPIO_FUNC_SIO, false, false, false, false, true);
    gpio_conf(IDE_DIOR,       GPIO_FUNC_SIO, false, false, false, false, true);
    gpio_conf(IDE_DA0,        GPIO_FUNC_SIO, false, false, false, false, true);
    gpio_conf(IDE_DA1,        GPIO_FUNC_SIO, false, false, false, false, true);
    gpio_conf(IDE_DA2,        GPIO_FUNC_SIO, false, false, false, false, true);
    gpio_conf(IDE_CS0,        GPIO_FUNC_SIO, false, false, false, false, true);
    gpio_conf(IDE_CS1,        GPIO_FUNC_SIO, false, false, false, false, true);
    gpio_conf(IDE_DMACK,      GPIO_FUNC_SIO, false, false, false, false, true);

    // IDE bus data signals
    for (int i = 0; i < 16; i++)
    {
        gpio_conf(IDE_D0 + i, GPIO_FUNC_SIO, false, false, false, false, true);
    }

    // Status LED
    gpio_conf(STATUS_LED,     GPIO_FUNC_SIO, false,false, true,  false, false);
}

// late_init() only runs in main application
void platform_late_init()
{

}

uint8_t platform_check_for_controller()
{
    return 0;
}

void platform_set_status_controller(zuluide::ObserverTransfer<zuluide::status::SystemStatus> *statusController) {
  
}

void platform_set_display_controller(zuluide::Observable<zuluide::control::DisplayState>& displayController) {
}

void platform_set_input_interface(zuluide::control::InputReceiver* inputReceiver) {
}

void platform_set_device_control(zuluide::status::DeviceControlSafe* deviceControl) {
}

void platform_poll_input() {
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

int platform_get_device_id(void)
{
    if (g_dip_cable_sel)
    {
        if (g_cable_sel_state)
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
    volatile uint core_num = get_core_num();
    if (core_num != 0)
    {
        logmsg("Only core 0 may attempt 'platform_emergency_log_save()'");
        return;
    }

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

static void dump_stack(uint32_t *sp)
{
    uint32_t *p = (uint32_t*)((uint32_t)sp & ~3);

    for (int i = 0; i < 16; i++)
    {
        if (p == &__StackTop) break; // End of stack

        logmsg("STACK ", (uint32_t)p, ":    ", p[0], " ", p[1], " ", p[2], " ", p[3]);
        p += 4;
    }
}

extern "C" __attribute__((noinline))
void show_hardfault(uint32_t *sp, uint32_t r4, uint32_t r5, uint32_t r6, uint32_t r7)
{
    uint32_t pc = sp[6];
    uint32_t lr = sp[5];

    // When the main firmware is run in non-secure mode, the secure mode
    // fault handler forwards to us. It stores context in r4-r7.
    bool sec_fault = (r4 == 0xACCE55ED);

    logmsg("--------------");
    logmsg("CRASH!");
    logmsg("Platform: ", g_platform_name);
    logmsg("FW Version: ", g_log_firmwareversion);
    logmsg("CFSR: ", (uint32_t)scb_hw->cfsr);
    logmsg("BFAR: ", (uint32_t)scb_hw->bfar);
    logmsg("SP: ", (uint32_t)sp);
    logmsg("PC: ", pc);
    logmsg("LR: ", lr);
    logmsg("R0: ", sp[0]);
    logmsg("R1: ", sp[1]);
    logmsg("R2: ", sp[2]);
    logmsg("R3: ", sp[3]);
    logmsg("R4: ", r4, (sec_fault ? " (SECURE FAULT)" : "")); // ARMv8-m secure mode handler forwards to us
    logmsg("R5: ", r5);
    logmsg("R6: ", r6, (sec_fault ? " (SFAR)" : ""));
    logmsg("R7: ", r7, (sec_fault ? " (SFSR)" : ""));

    dump_stack(sp);

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
    // Copies stack pointer and R4..R7 into function arguments
    asm("mrs r0, msp\n"
        "mov r1, r4\n"
        "mov r2, r5\n"
        "mov r3, r6\n"
        "mov r4, r7\n"
        "bl show_hardfault": : : "r0");
}

extern "C" void __assert_func(const char *file, int line, const char *func, const char *failedexpr)
{
    logmsg("Assert failed: ", file, ":", line, " ", func, " ", failedexpr);

    uint32_t stack = 0;
    dump_stack(&stack);

    platform_emergency_log_save();

    platform_set_blink_status(false);
    while(1)
    {
        usb_log_poll();
        LED_OFF();
        sleep_ms(100);
        LED_ON();
        sleep_ms(100);
    }
}

extern "C" void panic(const char *fmt, ...)
{
    logmsg("Panic: ", fmt);

    uint32_t stack = 0;
    dump_stack(&stack);

    platform_emergency_log_save();

    platform_set_blink_status(false);
    while(1)
    {
        usb_log_poll();
        LED_OFF();
        sleep_ms(100);
        LED_ON();
        sleep_ms(100);
    }
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

            uint32_t msp;
            asm volatile ("MRS %0, msp" : "=r" (msp) );
            uint32_t *p =  (uint32_t*)msp;
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

            uint32_t msp;
            asm volatile ("MRS %0, msp" : "=r" (msp) );
            uint32_t *p =  (uint32_t*)msp;
            for (int i = 0; i < 16; i++)
            {
                if (p == &__StackTop) break; // End of stack

                logmsg("STACK ", (uint32_t)p, ":    ", p[0], " ", p[1], " ", p[2], " ", p[3]);
                p += 4;
            }


            usb_log_poll();
            platform_emergency_log_save();

#ifndef RP2040_DISABLE_BOOTLOADER
           platform_boot_to_main_firmware();
#else
            // copied from CMSIS's __NVIC_SystemReset();
            asm volatile ("dsb 0xF":::"memory");
            scb_hw->aircr  = ((0x5FAUL << 16U) | 1UL << 2U);
            asm volatile ("dsb 0xF":::"memory");
            while(true);
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
        for (int i = 0; i < NUM_GENERIC_TIMERS; i++)
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

void usb_command_handler(char *cmd)
{
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

// Pass forwards log messages from core1 code.
void core1_log_poll()
{
    static uint32_t core1_log_readpos = 0;

    while (g_idecomm.logpos > core1_log_readpos)
    {
        char linebuf[128];
        int len = 0;
        while (len < sizeof(linebuf) - 1 && g_idecomm.logpos > core1_log_readpos)
        {
            char c = g_idecomm.core1_log[core1_log_readpos % sizeof(g_idecomm.core1_log)];
            core1_log_readpos++;

            if (c == '\n' || c == '\r')
            {
                if (len > 0)
                    break;
                else
                    continue;
            }

            linebuf[len++] = c;
        }

        if (len > 0)
        {
            linebuf[len] = '\0';
            logmsg("CORE1: ", linebuf);
        }
    }
}

// Poll function that is called every few milliseconds.
// Can be left empty or used for platform-specific processing.
void platform_poll()
{
    static uint32_t prev_poll_time;

    // Disable the RP2040Support.h interrupt, otherwise it steals our FIFO items
    // TODO: Find a better place to do this, it has to be after
    // framework-arduinopico/cores/rp2040/main.cpp runs.
    irq_set_enabled(SIO_IRQ_FIFO, false);

    // No point polling the USB hardware more often than once per millisecond
    uint32_t time_now = millis();
    if (time_now == prev_poll_time)
    {
        return;
    }
    prev_poll_time = time_now;

    // Monitor supply voltage and process USB events
    adc_poll();
    core1_log_poll();
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

// Check if we are currently running in ARM Secure state.
// If not, we need to reboot before reflashing.
static bool is_secure_mode()
{
    // This uses the TT instruction to check if the current
    // running location is configured as secure memory.
    uint32_t addr = (uint32_t)&is_secure_mode;
    uint32_t result = 0;
    asm("tt %0, %1" : "=r"(result) : "r"(addr) : "cc");
    return result & (1 << 22);
}

// Reboot and instruct the encrypted code bootloader to boot main firmware in secure mode.
// Core1 code will not be available to run, so normal IDE functionality is not available
// until next reboot.
static void reboot_to_secure()
{
    watchdog_hw->scratch[7] = 0x70ADC0DE;
    watchdog_hw->ctrl = WATCHDOG_CTRL_TRIGGER_BITS;
    while(1);
}

#define BOOTLOADER_OFFSET (128 * 1024)
#define MAINAPP_OFFSET (256 * 1024)

__attribute__((section(".time_critical.platform_rewrite_flash_page")))
bool platform_rewrite_flash_page(uint32_t offset, uint8_t buffer[PLATFORM_FLASH_PAGE_SIZE])
{
    if (offset == 0)
    {
        // Recognize either the magic word that starts the encrypted HSL library,
        // or the start of a normal unencrypted vector table. This is used to reject
        // clearly garbage files.
        bool is_hsl = (buffer[0] == 0x5A && buffer[1] == 0x48 && buffer[2] == 0x53 && buffer[3] == 0x4C);
        bool is_raw = (buffer[3] == 0x20 && buffer[7] == 0x10);

        if (!is_hsl && !is_raw)
        {
            logmsg("Invalid firmware file, starts with: ", bytearray(buffer, 16));
            return false;
        }
    }

    if (offset >= BOOTLOADER_OFFSET && offset < MAINAPP_OFFSET)
    {
        logmsg("Skipping bootloader area at offset ", offset);
        return true;
    }

#ifdef ARM_NONSECURE_MODE
    if (!is_secure_mode())
    {
        logmsg("Rebooting to secure mode for flashing");
        reboot_to_secure();
    }
#endif

    if (nvic_hw->iser[0] & (1 << 14))
    {
        logmsg("Disabling USB during firmware flashing");
        nvic_hw->icer[0] = 1 << 14;
        usb_hw->main_ctrl = 0;
    }

    dbgmsg("Writing flash at offset ", offset, " data ", bytearray(buffer, 4));
    assert(offset % PLATFORM_FLASH_PAGE_SIZE == 0);
    assert(offset >= PLATFORM_BOOTLOADER_SIZE);

    // Avoid any mbed timer interrupts triggering during the flashing.
    uint32_t saved_irq = save_and_disable_interrupts();

    flash_range_erase(offset, PLATFORM_FLASH_PAGE_SIZE);
    flash_range_program(offset, buffer, PLATFORM_FLASH_PAGE_SIZE);

    uint32_t *buf32 = (uint32_t*)buffer;
    uint32_t num_words = PLATFORM_FLASH_PAGE_SIZE / 4;
    for (int i = 0; i < num_words; i++)
    {
        uint32_t expected = buf32[i];
        uint32_t actual = *(volatile uint32_t*)(XIP_NOCACHE_NOALLOC_BASE + offset + i * 4);

        if (actual != expected)
        {
            logmsg("Flash verify failed at offset ", offset + i * 4, " got ", actual, " expected ", expected);
            return false;
        }
    }

    restore_interrupts(saved_irq);

    return true;
}

void platform_boot_to_main_firmware()
{
#ifdef ARM_NONSECURE_MODE
    if (is_secure_mode())
    {
        // Reboot to non-secure mode and the reset handler will jump to main firmware.
        watchdog_hw->scratch[6] = 0xB007;
        watchdog_hw->scratch[7] = 0;
        watchdog_hw->ctrl = WATCHDOG_CTRL_TRIGGER_BITS;
    }
#endif

    // Jump directly to main firmware
    uint32_t *application_base = (uint32_t*)(XIP_BASE + MAINAPP_OFFSET);
    scb_hw->vtor = (uint32_t)application_base;
    __asm__(
        "msr msp, %0\n\t"
        "bx %1" : : "r" (application_base[0]),
                    "r" (application_base[1]) : "memory");
}

void btldr_reset_handler()
{
    uint32_t* application_base = &__real_vectors_start;
    if (watchdog_hw->scratch[6] == 0xB007)
    {
        // Boot to main application
        watchdog_hw->scratch[6] = 0;
        application_base = (uint32_t*)(XIP_BASE + MAINAPP_OFFSET);
    }

    scb_hw->vtor = (uint32_t)application_base;
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
}

mutex_t* platform_get_log_mutex() {
  return &logMutex;
}

void processStatusUpdate(const zuluide::status::SystemStatus &currentStatus) {
}

/********************************/
/* Logic sniffer functionality  */
/********************************/

bool platform_enable_sniffer(const char *filename, bool passive)
{
    if (passive)
    {
        // Stop IDE phy and configure pins for passive input
        g_idecomm.enable_idephy = false;

        // Configure IDE bus for input mode
        // Data buffer is enabled in input mode.
        // Control buffer is disabled.
        for (int i = 0; i < 16; i++)
        {
            gpio_conf(IDE_D0 + i, GPIO_FUNC_SIO, false, false, false, false, true);
        }

        //        pin             function       pup   pdown  out    state fast
        gpio_conf(IDE_RST,        GPIO_FUNC_SIO, false, false, false, false, true); // Input
        gpio_conf(IDE_DIOW,       GPIO_FUNC_SIO, false, false, false, false, true);
        gpio_conf(IDE_DIOR,       GPIO_FUNC_SIO, false, false, false, false, true);
        gpio_conf(IDE_DA0,        GPIO_FUNC_SIO, false, false, false, false, true);
        gpio_conf(IDE_DA1,        GPIO_FUNC_SIO, false, false, false, false, true);
        gpio_conf(IDE_DA2,        GPIO_FUNC_SIO, false, false, false, false, true);
        gpio_conf(IDE_CS0,        GPIO_FUNC_SIO, false, false, false, false, true);
        gpio_conf(IDE_CS1,        GPIO_FUNC_SIO, false, false, false, false, true);
        gpio_conf(IDE_DMACK,      GPIO_FUNC_SIO, false, false, false, false, true);
        gpio_conf(CTRL_LOAD,      GPIO_FUNC_SIO, false, false, true, false, true); // Don't load ctrl mux
        gpio_conf(CTRL_nEN,       GPIO_FUNC_SIO, false, false, true, true, true);  // Disable ctrl mux
        gpio_conf(CTRL_IN_SEL,    GPIO_FUNC_SIO, false, false, true, true, true);  // Disable ctrl in mux
        gpio_conf(IDE_IORDY_EN,   GPIO_FUNC_SIO, false, false, true, true, true);  // Disable IORDY out
        gpio_conf(IDE_IOCS16,     GPIO_FUNC_SIO, false, false, true, true, true);  // Disable IOCS16 out
        gpio_conf(IDE_DATADIR,    GPIO_FUNC_SIO, false, false, true, false, true); // Data input
        gpio_conf(IDE_DATASEL,    GPIO_FUNC_SIO, false, false, true, false, true); // Enable data buffer
    }

    g_sniffer_enabled = true;
    return rp2350_sniffer_init(filename, passive);
}

void platform_sniffer_poll()
{
    rp2350_sniffer_poll();
}

/*************************************************/
/* SDK overrides for running in non-secure mode  */
/*************************************************/

#ifdef ARM_NONSECURE_MODE
// When the main firmware runs under ARM TrustZone, we must
// override some SDK functions. These have already been
// initialized by the bootloader.

extern "C" void runtime_init_bootrom_reset()
{
}

extern "C" void runtime_init_early_resets()
{
}

extern "C" void runtime_init_post_clock_resets()
{
    // JTAG and TBMAN peripherals are secure access only,
    // so we have to mask them out of reset.
    unreset_block_mask_wait_blocking(0x1fbffeff);
}

extern "C" void runtime_init_boot_locks_reset()
{
}

extern "C" void runtime_init_bootrom_locking_enable()
{
}

extern "C" void runtime_init_per_core_bootrom_reset()
{
}

#endif

// These functions are not used for SDIO mode but are needed to avoid build error.
void sdCsInit(SdCsPin_t pin) {}
void sdCsWrite(SdCsPin_t pin, bool level) {}

// SDIO configuration for main program
SdioConfig g_sd_sdio_config(DMA_SDIO);

void platform_set_sd_callback(sd_callback_t func, const uint8_t *buffer)
{
    rp2350_sdio_sdfat_set_callback(func, buffer);
}
