#include "ZuluIDE_platform.h"
#include "ZuluIDE_log.h"
#include "ZuluIDE_config.h"
#include <SdFat.h>
#include <assert.h>
#include <hardware/gpio.h>
#include <hardware/uart.h>
#include <hardware/spi.h>
#include <hardware/structs/xip_ctrl.h>
#include <platform/mbed_error.h>
#include <multicore.h>

extern "C" {

// As of 2022-09-13, the platformio RP2040 core is missing cplusplus guard on flash.h
// For that reason this has to be inside the extern "C" here.
#include <hardware/flash.h>
#include "rp2040_flash_do_cmd.h"

const char *g_azplatform_name = PLATFORM_NAME;
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

void azplatform_init()
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
    uart_init(uart0, 1000000);
    g_uart_initialized = true;
    mbed_set_error_hook(mbed_error_hook);

    azlog("Platform: ", g_azplatform_name);
    azlog("FW Version: ", g_azlog_firmwareversion);

    azlog("DIP switch settings: cablesel ", (int)cablesel, ", drive_id ", (int)drive_id, " debug log ", (int)dbglog);

    g_azlog_debug = dbglog;
    
    // Get flash chip size
    uint8_t cmd_read_jedec_id[4] = {0x9f, 0, 0, 0};
    uint8_t response_jedec[4] = {0};
    flash_do_cmd(cmd_read_jedec_id, response_jedec, 4);
    g_flash_chip_size = (1 << response_jedec[3]);
    azlog("Flash chip size: ", (int)(g_flash_chip_size / 1024), " kB");

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

    /* Initialize IDE pins to required modes.
     * IDE pins should be inactive / input at this point.
     */

    // 16-bit data bus is used for communicating with the host and for loading
    // the control mux register. Set up the pull resistors so that IDE bus is
    // kept free when LED is blinked.
    for (int i = 0; i < 16; i++)
    {
        bool pull = (CR_IDLE_VALUE & (1 << i));
        //          pin             function       pup   pdown  out    state fast
        gpio_conf(IDE_IO_SHIFT + i, GPIO_FUNC_SIO, pull, !pull, false, true, true);
    }

    // MUX_SEL toggles to load the control register
    // Initial state is low and it keeps the data buffers off.
    //        pin             function       pup   pdown  out    state fast
    gpio_conf(MUX_SEL, GPIO_FUNC_SIO, false, false, true, false, true);

    // IDE bus transfer strobes are connected to GPIOs.
    // The bus drivers are off until enabled through control register.
    //        pin             function       pup   pdown  out    state fast
    gpio_conf(IDE_OUT_IORDY, GPIO_FUNC_SIO, false, false, true,  false, true);
    gpio_conf(IDE_IN_DIOR,   GPIO_FUNC_SIO, false, false, false, false, true);
    gpio_conf(IDE_IN_DIOW,   GPIO_FUNC_SIO, false, false, false, false, true);

    // IDE reset signal also resets the bus buffers, to ensure they are low.
    // To re-enable after a reset, the firmware needs to pull up IDE_IN_RST
    // and repeatedly write CTRL_IN bit until the IDE host has raised RST.
    //        pin             function       pup   pdown  out    state fast
    gpio_conf(IDE_IN_RST,    GPIO_FUNC_SIO, true,  false, false, false, true);
}

// late_init() only runs in main application
void azplatform_late_init()
{

}

void azplatform_write_led(bool state)
{
    if (g_led_disabled) return;

    // This codepath is used before the IDE PHY code is initialized.
    // It keeps the IDE bus free while writing the LED state
    gpio_put(MUX_SEL, false);
    gpio_set_pulls(CR_STATUS_LED, state, !state);
    delayMicroseconds(1);
    gpio_put(MUX_SEL, true);
}

void azplatform_disable_led(void)
{   
    g_led_disabled = true;
    azlog("Disabling status LED");
}

/*****************************************/
/* Crash handlers                        */
/*****************************************/

extern SdFs SD;
extern uint32_t __StackTop;

void azplatform_emergency_log_save()
{
    azplatform_set_sd_callback(NULL, NULL);

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
    crashfile.write(azlog_get_buffer(&startpos));
    crashfile.write(azlog_get_buffer(&startpos));
    crashfile.flush();
    crashfile.close();
}

void mbed_error_hook(const mbed_error_ctx * error_context)
{
    azlog("--------------");
    azlog("CRASH!");
    azlog("Platform: ", g_azplatform_name);
    azlog("FW Version: ", g_azlog_firmwareversion);
    azlog("error_status: ", (uint32_t)error_context->error_status);
    azlog("error_address: ", error_context->error_address);
    azlog("error_value: ", error_context->error_value);

    uint32_t *p = (uint32_t*)((uint32_t)error_context->thread_current_sp & ~3);
    for (int i = 0; i < 8; i++)
    {
        if (p == &__StackTop) break; // End of stack

        azlog("STACK ", (uint32_t)p, ":    ", p[0], " ", p[1], " ", p[2], " ", p[3]);
        p += 4;
    }

    azplatform_emergency_log_save();

    while (1)
    {
        // Flash the crash address on the LED
        // Short pulse means 0, long pulse means 1
        int base_delay = 1000;
        for (int i = 31; i >= 0; i--)
        {
            LED_OFF();
            for (int j = 0; j < base_delay; j++) delay_ns(100000);

            int delay = (error_context->error_address & (1 << i)) ? (3 * base_delay) : base_delay;
            LED_ON();
            for (int j = 0; j < delay; j++) delay_ns(100000);
            LED_OFF();
        }

        for (int j = 0; j < base_delay * 10; j++) delay_ns(100000);
    }
}

/*****************************************/
/* Debug logging and watchdog            */
/*****************************************/

// This function is called for every log message.
void azplatform_log(const char *s)
{
    if (g_uart_initialized)
    {
        uart_puts(uart0, s);
    }
}

static int g_watchdog_timeout;
static bool g_watchdog_initialized;

static void watchdog_callback(unsigned alarm_num)
{
    g_watchdog_timeout -= 1000;

    if (g_watchdog_timeout <= WATCHDOG_CRASH_TIMEOUT - WATCHDOG_BUS_RESET_TIMEOUT)
    {
        if (false)
        {
            azlog("--------------");
            azlog("WATCHDOG TIMEOUT, attempting bus reset");
            azlog("GPIO states: out ", sio_hw->gpio_out, " oe ", sio_hw->gpio_oe, " in ", sio_hw->gpio_in);

            uint32_t *p = (uint32_t*)__get_PSP();
            for (int i = 0; i < 8; i++)
            {
                if (p == &__StackTop) break; // End of stack

                azlog("STACK ", (uint32_t)p, ":    ", p[0], " ", p[1], " ", p[2], " ", p[3]);
                p += 4;
            }
        }

        if (g_watchdog_timeout <= 0)
        {
            azlog("--------------");
            azlog("WATCHDOG TIMEOUT!");
            azlog("Platform: ", g_azplatform_name);
            azlog("FW Version: ", g_azlog_firmwareversion);
            azlog("GPIO states: out ", sio_hw->gpio_out, " oe ", sio_hw->gpio_oe, " in ", sio_hw->gpio_in);

            uint32_t *p = (uint32_t*)__get_PSP();
            for (int i = 0; i < 8; i++)
            {
                if (p == &__StackTop) break; // End of stack

                azlog("STACK ", (uint32_t)p, ":    ", p[0], " ", p[1], " ", p[2], " ", p[3]);
                p += 4;
            }

            azplatform_emergency_log_save();

            azplatform_boot_to_main_firmware();
        }
    }

    hardware_alarm_set_target(3, delayed_by_ms(get_absolute_time(), 1000));
}

// This function can be used to periodically reset watchdog timer for crash handling.
// It can also be left empty if the platform does not use a watchdog timer.
void azplatform_reset_watchdog()
{
    g_watchdog_timeout = WATCHDOG_CRASH_TIMEOUT;

    if (!g_watchdog_initialized)
    {
        hardware_alarm_claim(3);
        hardware_alarm_set_callback(3, &watchdog_callback);
        hardware_alarm_set_target(3, delayed_by_ms(get_absolute_time(), 1000));
        g_watchdog_initialized = true;
    }
}

/*****************************************/
/* Flash reprogramming from bootloader   */
/*****************************************/

#ifdef AZPLATFORM_BOOTLOADER_SIZE

extern uint32_t __real_vectors_start;
extern uint32_t __StackTop;
static volatile void *g_bootloader_exit_req;

bool azplatform_rewrite_flash_page(uint32_t offset, uint8_t buffer[AZPLATFORM_FLASH_PAGE_SIZE])
{
    if (offset == AZPLATFORM_BOOTLOADER_SIZE)
    {
        if (buffer[3] != 0x20 || buffer[7] != 0x10)
        {
            azlog("Invalid firmware file, starts with: ", bytearray(buffer, 16));
            return false;
        }
    }

    azdbg("Writing flash at offset ", offset, " data ", bytearray(buffer, 4));
    assert(offset % AZPLATFORM_FLASH_PAGE_SIZE == 0);
    assert(offset >= AZPLATFORM_BOOTLOADER_SIZE);

    // Avoid any mbed timer interrupts triggering during the flashing.
    __disable_irq();

    // For some reason any code executed after flashing crashes
    // unless we disable the XIP cache.
    // Not sure why this happens, as flash_range_program() is flushing
    // the cache correctly.
    // The cache is now enabled from bootloader start until it starts
    // flashing, and again after reset to main firmware.
    xip_ctrl_hw->ctrl = 0;

    flash_range_erase(offset, AZPLATFORM_FLASH_PAGE_SIZE);
    flash_range_program(offset, buffer, AZPLATFORM_FLASH_PAGE_SIZE);

    uint32_t *buf32 = (uint32_t*)buffer;
    uint32_t num_words = AZPLATFORM_FLASH_PAGE_SIZE / 4;
    for (int i = 0; i < num_words; i++)
    {
        uint32_t expected = buf32[i];
        uint32_t actual = *(volatile uint32_t*)(XIP_NOCACHE_BASE + offset + i * 4);

        if (actual != expected)
        {
            azlog("Flash verify failed at offset ", offset + i * 4, " got ", actual, " expected ", expected);
            return false;
        }
    }

    __enable_irq();

    return true;
}

void azplatform_boot_to_main_firmware()
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
        application_base = (uint32_t*)(XIP_BASE + AZPLATFORM_BOOTLOADER_SIZE);
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

} /* extern "C" */

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
            azlog_raw(buf);
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
