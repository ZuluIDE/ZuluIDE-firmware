#include "rp2040_fpga.h"
#include "ZuluIDE_platform.h"
#include "ZuluIDE_config.h"
#include "ZuluIDE_log.h"
#include <hardware/gpio.h>
#include <hardware/spi.h>
#include <hardware/pio.h>
#include <hardware/clocks.h>
#include <hardware/structs/iobank0.h>
#include "fpga_bitstream.h"
#include "rp2040_fpga_qspi.pio.h"

#define FPGA_QSPI_PIO pio0
#define FPGA_QSPI_PIO_SM 0

static struct {
    bool claimed;
    uint32_t pio_offset_qspi_transfer;
    pio_sm_config pio_cfg_qspi_transfer_8bit;
    pio_sm_config pio_cfg_qspi_transfer_32bit;
} g_fpga_qspi;

static void fpga_io_as_spi()
{
    gpio_set_function(FPGA_SCK, GPIO_FUNC_SPI);
    gpio_set_function(FPGA_MISO, GPIO_FUNC_SPI);
    gpio_set_function(FPGA_MOSI, GPIO_FUNC_SPI);
    gpio_set_dir(FPGA_SCK, true);
    gpio_set_dir(FPGA_MISO, true);
    gpio_set_dir(FPGA_MISO, true);
}

static void fpga_io_as_qspi()
{
    gpio_set_function(FPGA_QSPI_SCK, GPIO_FUNC_PIO0);
    gpio_set_function(FPGA_QSPI_D0, GPIO_FUNC_PIO0);
    gpio_set_function(FPGA_QSPI_D1, GPIO_FUNC_PIO0);
    gpio_set_function(FPGA_QSPI_D2, GPIO_FUNC_PIO0);
    gpio_set_function(FPGA_QSPI_D3, GPIO_FUNC_PIO0);
}

static bool fpga_load_bitstream()
{
    // Refer to "CPU Configuration Procedure" in Lattice TN-02001
    
    // Reset to SPI peripheral configuration mode
    gpio_put(FPGA_CRESET, 0);
    gpio_put(FPGA_SS, 0);
    fpga_io_as_spi();
    gpio_put(FPGA_CRESET, 1);
    delay(2);

    // Initialize SPI bus used for configuration
    // ICE5LP1K supports 1-25 MHz baudrate
    // The bitstream size is 70 kB
    _spi_init(FPGA_SPI, 10000000);
    spi_set_format(FPGA_SPI, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    // Dummy clocks with chip unselected
    gpio_put(FPGA_SS, 1);
    uint8_t dummy = 0x00;
    spi_write_blocking(FPGA_SPI, &dummy, 1);
    
    // Send bitstream
    gpio_put(FPGA_SS, 0);
    spi_write_blocking(FPGA_SPI, fpga_bitstream, sizeof(fpga_bitstream));

    // Wait for configuration to complete (100 cycles = 13 bytes)
    uint8_t dummy2[13] = {0xFF};
    spi_write_blocking(FPGA_SPI, dummy2, 13);
    bool got_cdone = gpio_get(FPGA_CDONE);

    // Wait for user GPIO to be released (49 cycles = 7 bytes)
    gpio_put(FPGA_SS, 1);
    spi_write_blocking(FPGA_SPI, dummy2, 7);
    
    return got_cdone;
}

static void fpga_qspi_pio_init()
{
    if (!g_fpga_qspi.claimed)
    {
        g_fpga_qspi.claimed = true;
        pio_sm_claim(FPGA_QSPI_PIO, FPGA_QSPI_PIO_SM);
        pio_clear_instruction_memory(FPGA_QSPI_PIO);
        g_fpga_qspi.pio_offset_qspi_transfer = pio_add_program(FPGA_QSPI_PIO, &fpga_qspi_transfer_program);
    
        pio_sm_config cfg = fpga_qspi_transfer_program_get_default_config(g_fpga_qspi.pio_offset_qspi_transfer);
        sm_config_set_in_pins(&cfg, FPGA_QSPI_D0);
        sm_config_set_out_pins(&cfg, FPGA_QSPI_D0, 4);
        sm_config_set_sideset_pins(&cfg, FPGA_QSPI_SCK);
        sm_config_set_in_shift(&cfg, true, true, 8);
        sm_config_set_out_shift(&cfg, true, true, 8);
        sm_config_set_clkdiv(&cfg, 8);
        g_fpga_qspi.pio_cfg_qspi_transfer_8bit = cfg;

        sm_config_set_in_shift(&cfg, true, true, 32);
        sm_config_set_out_shift(&cfg, true, true, 32);
        g_fpga_qspi.pio_cfg_qspi_transfer_32bit = cfg;

        // Bypass QSPI data pin synchronizer because the clocks are in same domain
        FPGA_QSPI_PIO->input_sync_bypass |= (0xF << FPGA_QSPI_D0);
    }
}

bool fpga_init()
{
    // Enable clock output to FPGA
    // 15.6 MHz for now, resulting in FPGA clock of 60MHz.
    gpio_set_function(FPGA_CLK, GPIO_FUNC_GPCK);
    gpio_set_dir(FPGA_CLK, true);
    clock_gpio_init(FPGA_CLK, CLOCKS_CLK_GPOUT0_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS, 8);

    // Load bitstream
    if (!fpga_load_bitstream())
    {
        logmsg("FPGA bitstream loading failed");
        return false;
    }
    
    // Set pins to QSPI mode
    fpga_io_as_qspi();
    fpga_qspi_pio_init();

    // Test communication
    uint32_t result1, result2, result3;
    fpga_rdcmd(0x7F, (uint8_t*)&result1, 4);
    delay(1);
    fpga_rdcmd(0x7F, (uint8_t*)&result2, 4);
    delay(1);
    fpga_rdcmd(0x7F, (uint8_t*)&result3, 4);
    
    uint32_t expected = 0x03020100;
    if (result1 != expected || result2 != expected || result3 != expected)
    {
        logmsg("FPGA communication test failed, got ", result1, " ", result2, " ", result3, " expected ", expected);
        return false;
    }

    return true;
}

static void fpga_start_cmd(uint8_t cmd)
{
    // Prepare for start of new command, raise chip select and init PIO in 8-bit write mode
    gpio_put(FPGA_SS, 1);
    pio_sm_init(FPGA_QSPI_PIO, FPGA_QSPI_PIO_SM,
                g_fpga_qspi.pio_offset_qspi_transfer,
                &g_fpga_qspi.pio_cfg_qspi_transfer_8bit);
    pio_sm_set_consecutive_pindirs(FPGA_QSPI_PIO, FPGA_QSPI_PIO_SM, FPGA_QSPI_SCK, 1, true);
    pio_sm_set_consecutive_pindirs(FPGA_QSPI_PIO, FPGA_QSPI_PIO_SM, FPGA_QSPI_D0, 4, true);
    pio_sm_set_enabled(FPGA_QSPI_PIO, FPGA_QSPI_PIO_SM, true);

    // Activate chip select and transfer command
    gpio_put(FPGA_SS, 0);
    pio_sm_put_blocking(FPGA_QSPI_PIO, FPGA_QSPI_PIO_SM, cmd);
}

static void fpga_release()
{
    gpio_put(FPGA_SS, 1);
    pio_sm_set_enabled(FPGA_QSPI_PIO, FPGA_QSPI_PIO_SM, false);
    pio_sm_set_consecutive_pindirs(FPGA_QSPI_PIO, FPGA_QSPI_PIO_SM, FPGA_QSPI_D0, 4, false);
}

void fpga_wrcmd(uint8_t cmd, const uint8_t *payload, size_t payload_len, bool keep_active)
{
    // Expecting a write-mode command
    assert(cmd & 0x80);

    // Start transfer and write command byte
    fpga_start_cmd(cmd);

    // Transfer data, if any
    for (size_t i = 0; i < payload_len; i++)
    {
        pio_sm_put_blocking(FPGA_QSPI_PIO, FPGA_QSPI_PIO_SM, payload[i]);
    }

    if (!keep_active)
    {
        // Raise chip select if not continuing
        fpga_release();
    }
    else
    {
        // Configure in 32-bit mode for data transfer
        pio_sm_set_enabled(FPGA_QSPI_PIO, FPGA_QSPI_PIO_SM, false);
        pio_sm_init(FPGA_QSPI_PIO, FPGA_QSPI_PIO_SM,
                g_fpga_qspi.pio_offset_qspi_transfer,
                &g_fpga_qspi.pio_cfg_qspi_transfer_32bit);
        pio_sm_set_enabled(FPGA_QSPI_PIO, FPGA_QSPI_PIO_SM, true);
    }
}

void fpga_rdcmd(uint8_t cmd, uint8_t *result, size_t result_len, bool keep_active)
{
    // Expecting a read-mode command
    assert(!(cmd & 0x80));

    // Start transfer and write command byte
    fpga_start_cmd(cmd);

    // Change to read mode with bus turnaround byte
    pio_sm_get_blocking(FPGA_QSPI_PIO, FPGA_QSPI_PIO_SM);
    pio_sm_set_consecutive_pindirs(FPGA_QSPI_PIO, FPGA_QSPI_PIO_SM, FPGA_QSPI_D0, 4, false);
    pio_sm_put_blocking(FPGA_QSPI_PIO, FPGA_QSPI_PIO_SM, 0xFF);
    pio_sm_get_blocking(FPGA_QSPI_PIO, FPGA_QSPI_PIO_SM);

    // Transfer data, if any
    for (size_t i = 0; i < result_len; i++)
    {
        pio_sm_put_blocking(FPGA_QSPI_PIO, FPGA_QSPI_PIO_SM, 0xFF);
        result[i] = pio_sm_get_blocking(FPGA_QSPI_PIO, FPGA_QSPI_PIO_SM) >> 24;
    }

    if (!keep_active)
    {
        // Raise chip select if not continuing
        fpga_release();
    }
    else
    {
        // Configure in 32-bit mode for data transfer
        pio_sm_set_enabled(FPGA_QSPI_PIO, FPGA_QSPI_PIO_SM, false);
        pio_sm_init(FPGA_QSPI_PIO, FPGA_QSPI_PIO_SM,
                g_fpga_qspi.pio_offset_qspi_transfer,
                &g_fpga_qspi.pio_cfg_qspi_transfer_32bit);
        pio_sm_set_enabled(FPGA_QSPI_PIO, FPGA_QSPI_PIO_SM, true);
    }
}
