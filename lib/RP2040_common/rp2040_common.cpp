/**
 * ZuluIDE™ - Copyright (c) 2025 Rabbit Hole Computing™
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
#include "rp2040_common.h"
#include <ZuluIDE_config.h>
#include <ZuluIDE_platform_gpio.h>
#include <ZuluIDE_log.h>
#include <hardware/flash.h>
#include <hardware/structs/xip_ctrl.h>
#include <hardware/structs/usb.h>
#include <class/cdc/cdc_device.h>
#include "rp2040_fpga.h"
#include <SdFat.h>

const char *g_platform_name = PLATFORM_NAME;
static bool g_led_disabled = false;
static bool g_led_blinking = false;
static bool g_uart_initialized = false;
static bool g_dip_drive_id, g_dip_cable_sel;
static mutex_t logMutex;

/***************/
/* GPIO init   */
/***************/

// Helper function to configure whole GPIO in one line
void gpio_conf(uint gpio, gpio_function_t fn, bool pullup, bool pulldown, bool output, bool initial_state, bool fast_slew)
{
    gpio_put(gpio, initial_state);
    gpio_set_dir(gpio, output);
    gpio_set_pulls(gpio, pullup, pulldown);
    gpio_set_function(gpio, fn);

    if (fast_slew)
    {
        pads_bank0_hw->io[gpio] |= PADS_BANK0_GPIO0_SLEWFAST_BITS;
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
    mutex_init(&logMutex);

    g_log_debug = !gpio_get(DIP_DBGLOG);
    g_dip_cable_sel = !gpio_get(DIP_CABLESEL);
    g_dip_drive_id = !gpio_get(DIP_DRIVE_ID);

    logmsg("Platform: ", g_platform_name);
    logmsg("FW Version: ", g_log_firmwareversion);

    logmsg("DIP switch settings: cablesel ", (int)g_dip_cable_sel, ", drive_id ", (int)g_dip_drive_id, " debug log ", (int)g_log_debug);


    /* Initialize logging to SWO pin (UART0) */
    gpio_conf(SWO_PIN,        GPIO_FUNC_UART,false,false, true,  false, true);
    uart_init(uart0, 1000000); // Debug UART at 1 MHz baudrate
    g_uart_initialized = true;

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

static int g_watchdog_timeout;
static bool g_watchdog_initialized;
static bool g_watchdog_did_bus_reset;

void ide_phy_reset_from_watchdog();

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

    if (nvic_hw->iser & 1 << 14)
    {
        logmsg("Disabling USB during firmware flashing");
        nvic_hw->icer = 1 << 14;
        usb_hw->main_ctrl = 0;
    }

    dbgmsg("Writing flash at offset ", offset, " data ", bytearray(buffer, 4));
    assert(offset % PLATFORM_FLASH_PAGE_SIZE == 0);
    assert(offset >= PLATFORM_BOOTLOADER_SIZE);

    // Avoid any mbed timer interrupts triggering during the flashing.
    uint32_t saved_irq = save_and_disable_interrupts();

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

    restore_interrupts(saved_irq);

    return true;
}

void platform_boot_to_main_firmware()
{
    // To ensure that the system state is reset properly, we perform
    // a SYSRESETREQ and jump straight from the reset vector to main application.
    g_bootloader_exit_req = &g_bootloader_exit_req;
    scb_hw->aircr = 0x05FA0004;
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
void usb_log_poll()
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


/*****************************************/
/* Crash handlers                        */
/*****************************************/

extern SdFs SD;
extern uint32_t __StackTop;
void usb_log_poll();

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


// This function is called for every log message.
void platform_log(const char *s)
{
    if (g_uart_initialized)
    {
        uart_puts(uart0, s);
    }
}

mutex_t* platform_get_log_mutex() {
  return &logMutex;
}