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
    pio_sm_get_blocking(FPGA_QSPI_PIO, FPGA_QSPI_PIO_SM);

    // Transfer data, if any
    for (size_t i = 0; i < payload_len; i++)
    {
        pio_sm_put_blocking(FPGA_QSPI_PIO, FPGA_QSPI_PIO_SM, payload[i]);
        pio_sm_get_blocking(FPGA_QSPI_PIO, FPGA_QSPI_PIO_SM);
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

static const char *traceregname(uint8_t idx)
{
    // Matches ide_reg_addr_t on FPGA side
    switch (idx & 15)
    {
        case  0: return "INVALID";
        case  1: return "DATA";
        case  2: return "ALT_STATUS";
        case  3: return "STATUS";
        case  4: return "COMMAND";
        case  5: return "DEVICE";
        case  6: return "DEVICE_CONTROL";
        case  7: return "ERRORREG";
        case  8: return "FEATURE";
        case  9: return "SECTOR_COUNT";
        case 10: return "LBA_LOW";
        case 11: return "LBA_MID";
        case 12: return "LBA_HIGH";
        default: return "??";
    }
}

void fpga_dump_ide_regs()
{
    uint8_t regs[10];
    fpga_rdcmd(FPGA_CMD_READ_IDE_REGS, regs, 10);
    dbgmsg("-- IDE registers:",
        " STATUS:", regs[0],
        " COMMAND:", regs[1],
        " DEVICE:", regs[2],
        " DEVICE_CONTROL:", regs[3],
        " ERROR:", regs[4],
        " FEATURE:", regs[5],
        " SECTOR_COUNT:", regs[6],
        " LBA_LOW:", regs[7],
        " LBA_MID:", regs[8],
        " LBA_HIGH:", regs[9]
        );
}

void fpga_dump_tracelog()
{
    uint8_t tracebuf[257];
    fpga_rdcmd(FPGA_CMD_READ_TRACEBUF, tracebuf, 257);

    dbgmsg("-- FPGA trace begins");

    uint8_t *p = tracebuf + 1;
    while (p < tracebuf + sizeof(tracebuf))
    {
        uint8_t b = *p++;
        if (b == 0x01)
        {
            dbgmsg("---- DATA READ");
        }
        else if (b == 0x02)
        {
            dbgmsg("---- DATA WRITE");
        }
        else if ((b >> 4) == 0x1)
        {
            uint8_t b2 = *p++;
            dbgmsg("---- REG READ ", traceregname(b), " ", b2);
        }
        else if ((b >> 4) == 0x2)
        {
            uint8_t b2 = *p++;
            dbgmsg("---- REG WRITE ", traceregname(b), " ", b2);
        }
        else if (b != 0x00)
        {
            dbgmsg("---- ", b);
        }
    }

    dbgmsg("-- FPGA trace ends");

    fpga_dump_ide_regs();
}