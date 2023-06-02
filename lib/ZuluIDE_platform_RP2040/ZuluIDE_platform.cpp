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
#include "ide_phy.h"
#include <SdFat.h>
#include <assert.h>
#include <hardware/gpio.h>
#include <hardware/uart.h>
#include <hardware/spi.h>
#include <hardware/structs/xip_ctrl.h>
#include <hardware/structs/iobank0.h>
#include <hardware/flash.h>
#include <platform/mbed_error.h>
#include <multicore.h>
#include <USB/PluggableUSBSerial.h>
#include "rp2040_fpga.h"

const char *g_platform_name = PLATFORM_NAME;
static uint32_t g_flash_chip_size = 0;
static bool g_uart_initialized = false;
static bool g_led_disabled = false;

void mbed_error_hook(const mbed_error_ctx * error_context);

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

void platform_init()
{
    // Make sure second core is stopped
    multicore_reset_core1();

    /* Check dip switch settings */
    gpio_conf(DIP_CABLESEL,     GPIO_FUNC_SIO, false, false, false, false, false);
    gpio_conf(DIP_DRIVE_ID,     GPIO_FUNC_SIO, false, false, false, false, false);
    gpio_conf(DIP_DBGLOG,       GPIO_FUNC_SIO, false, false, false, false, false);

    delay(10); // 10 ms delay to let pull-ups do their work

    bool dbglog = !gpio_get(DIP_DBGLOG);
    bool cablesel = !gpio_get(DIP_CABLESEL);
    bool drive_id = !gpio_get(DIP_DRIVE_ID);

    /* Initialize logging to SWO pin (UART0) */
    gpio_conf(SWO_PIN,        GPIO_FUNC_UART,false,false, true,  false, true);
    uart_init(uart0, 1000000); // Debug UART at 1 MHz baudrate
    g_uart_initialized = true;
    mbed_set_error_hook(mbed_error_hook);

    logmsg("Platform: ", g_platform_name);
    logmsg("FW Version: ", g_log_firmwareversion);

    logmsg("DIP switch settings: cablesel ", (int)cablesel, ", drive_id ", (int)drive_id, " debug log ", (int)dbglog);

    g_log_debug = dbglog;
    
    // Get flash chip size
    uint8_t cmd_read_jedec_id[4] = {0x9f, 0, 0, 0};
    uint8_t response_jedec[4] = {0};
    flash_do_cmd(cmd_read_jedec_id, response_jedec, 4);
    g_flash_chip_size = (1 << response_jedec[3]);
    logmsg("Flash chip size: ", (int)(g_flash_chip_size / 1024), " kB");

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
    //        pin             function       pup   pdown  out    state fast
    gpio_conf(GPIO_I2C_SCL,   GPIO_FUNC_I2C, true,false, false,  true, true);
    gpio_conf(GPIO_I2C_SDA,   GPIO_FUNC_I2C, true,false, false,  true, true);

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
        dbgmsg("FPGA initialization succeeded");
    }
    else
    {
        logmsg("FPGA initialization failed");
    }
}

void platform_write_led(bool state)
{
    if (g_led_disabled) return;

    gpio_put(STATUS_LED, state);
}

void platform_disable_led(void)
{   
    g_led_disabled = true;
    logmsg("Disabling status LED");
}

/*****************************************/
/* Crash handlers                        */
/*****************************************/

extern SdFs SD;
extern uint32_t __StackTop;

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

void mbed_error_hook(const mbed_error_ctx * error_context)
{
    logmsg("--------------");
    logmsg("CRASH!");
    logmsg("Platform: ", g_platform_name);
    logmsg("FW Version: ", g_log_firmwareversion);
    logmsg("error_status: ", (uint32_t)error_context->error_status);
    logmsg("error_address: ", error_context->error_address);
    logmsg("error_value: ", error_context->error_value);

    uint32_t *p = (uint32_t*)((uint32_t)error_context->thread_current_sp & ~3);
    for (int i = 0; i < 8; i++)
    {
        if (p == &__StackTop) break; // End of stack

        logmsg("STACK ", (uint32_t)p, ":    ", p[0], " ", p[1], " ", p[2], " ", p[3]);
        p += 4;
    }

    if (sio_hw->cpuid == 1)
    {
        // Don't try to save files from core 1
        // Core 0 will reset this to recover
        logmsg("--- CORE1 CRASH HANDLER END");
        while(1);
    }

    platform_emergency_log_save();

    while (1)
    {
        // Flash the crash address on the LED
        // Short pulse means 0, long pulse means 1
        int base_delay = 200000;
        for (int i = 31; i >= 0; i--)
        {
            LED_OFF();
            delayMicroseconds(base_delay);

            int delay = (error_context->error_address & (1 << i)) ? (3 * base_delay) : base_delay;
            LED_ON();
            delayMicroseconds(delay);
            LED_OFF();
        }

        delayMicroseconds(base_delay);
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

    if (_SerialUSB.ready())
    {
        // Retrieve pointer to log start and determine number of bytes available.
        uint32_t available = 0;
        const char *data = log_get_buffer(&logpos, &available);

        // Limit to CDC packet size
        uint32_t len = available;
        if (len == 0) return;
        if (len > CDC_MAX_PACKET_SIZE) len = CDC_MAX_PACKET_SIZE;

        // Update log position by the actual number of bytes sent
        // If USB CDC buffer is full, this may be 0
        uint32_t actual = 0;
        _SerialUSB.send_nb((uint8_t*)data, len, &actual);
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

            uint32_t *p = (uint32_t*)__get_PSP();
            for (int i = 0; i < 8; i++)
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

            uint32_t *p = (uint32_t*)__get_PSP();
            for (int i = 0; i < 8; i++)
            {
                if (p == &__StackTop) break; // End of stack

                logmsg("STACK ", (uint32_t)p, ":    ", p[0], " ", p[1], " ", p[2], " ", p[3]);
                p += 4;
            }

            fpga_dump_tracelog();

            usb_log_poll();
            platform_emergency_log_save();

#ifndef RP2040_DISABLE_BOOTLOADER
            platform_boot_to_main_firmware();
#else
            NVIC_SystemReset();
#endif
        }
    }

    hardware_alarm_set_target(3, delayed_by_ms(get_absolute_time(), 1000));
}

// This function can be used to periodically reset watchdog timer for crash handling.
// It can also be left empty if the platform does not use a watchdog timer.
void platform_reset_watchdog()
{
    g_watchdog_timeout = WATCHDOG_CRASH_TIMEOUT;
    g_watchdog_did_bus_reset = false;

    if (!g_watchdog_initialized)
    {
        hardware_alarm_claim(3);
        hardware_alarm_set_callback(3, &watchdog_callback);
        hardware_alarm_set_target(3, delayed_by_ms(get_absolute_time(), 1000));
        g_watchdog_initialized = true;
    }

    // USB log is polled here also to make sure any log messages in fault states
    // get passed to USB.
    usb_log_poll();
}

// Poll function that is called every few milliseconds.
// Can be left empty or used for platform-specific processing.
void platform_poll()
{
    usb_log_poll();
    adc_poll();
}

/*****************************************/
/* Flash reprogramming from bootloader   */
/*****************************************/

#ifdef PLATFORM_BOOTLOADER_SIZE

extern uint32_t __real_vectors_start;
extern uint32_t __StackTop;
static volatile void *g_bootloader_exit_req;

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

/* Logging from mbed */

static class LogTarget: public mbed::FileHandle {
public:
    virtual ssize_t read(void *buffer, size_t size) { return 0; }
    virtual ssize_t write(const void *buffer, size_t size)
    {
        // A bit inefficient but mbed seems to write() one character
        // at a time anyways.
        for (int i = 0; i < size; i++)
        {
            char buf[2] = {((const char*)buffer)[i], 0};
            log_raw(buf);
        }
        return size;
    }

    virtual off_t seek(off_t offset, int whence = SEEK_SET) { return offset; }
    virtual int close() { return 0; }
    virtual off_t size() { return 0; }
} g_LogTarget;

mbed::FileHandle *mbed::mbed_override_console(int fd)
{
    return &g_LogTarget;
}
