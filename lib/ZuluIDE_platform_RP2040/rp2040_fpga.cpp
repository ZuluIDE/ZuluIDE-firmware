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
#include <hardware/dma.h>
#include <hardware/clocks.h>
#include <hardware/structs/iobank0.h>
#include "fpga_bitstream.h"
#include "rp2040_fpga_qspi.pio.h"

#define FPGA_QSPI_PIO pio0
#define FPGA_QSPI_PIO_SM 0
#define FPGA_QSPI_DMA_TX 2
#define FPGA_QSPI_DMA_RX 3

static struct {
    bool claimed;
    bool bitstream_loaded;
    bool license_done;
    uint32_t pio_offset_qspi_transfer;
    pio_sm_config pio_cfg_qspi_transfer_8bit;
    pio_sm_config pio_cfg_qspi_transfer_32bit;

    dma_channel_config dma_tx_cfg;   // Transmit from unaligned buffer
    dma_channel_config dma_rx_cfg;   // Receive to unaligned buffer
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
    
        // Build PIO configurations
        {
            pio_sm_config cfg = fpga_qspi_transfer_program_get_default_config(g_fpga_qspi.pio_offset_qspi_transfer);
            sm_config_set_in_pins(&cfg, FPGA_QSPI_D0);
            sm_config_set_out_pins(&cfg, FPGA_QSPI_D0, 4);
            sm_config_set_sideset_pins(&cfg, FPGA_QSPI_SCK);
            sm_config_set_in_shift(&cfg, true, true, 8);
            sm_config_set_out_shift(&cfg, true, true, 8);
            g_fpga_qspi.pio_cfg_qspi_transfer_8bit = cfg;

            sm_config_set_in_shift(&cfg, true, true, 32);
            sm_config_set_out_shift(&cfg, true, true, 32);
            g_fpga_qspi.pio_cfg_qspi_transfer_32bit = cfg;

            // Bypass QSPI data pin synchronizer because the clocks are in same domain
            FPGA_QSPI_PIO->input_sync_bypass |= (0xF << FPGA_QSPI_D0);
        }

        // Build DMA channel configurations
        {
            dma_channel_claim(FPGA_QSPI_DMA_TX);
            dma_channel_claim(FPGA_QSPI_DMA_RX);

            dma_channel_config cfg = dma_channel_get_default_config(FPGA_QSPI_DMA_TX);
            channel_config_set_transfer_data_size(&cfg, DMA_SIZE_32);
            channel_config_set_read_increment(&cfg, true);
            channel_config_set_write_increment(&cfg, false);
            channel_config_set_dreq(&cfg, pio_get_dreq(FPGA_QSPI_PIO, FPGA_QSPI_PIO_SM, true));
            g_fpga_qspi.dma_tx_cfg = cfg;
            
            cfg = dma_channel_get_default_config(FPGA_QSPI_DMA_RX);
            channel_config_set_transfer_data_size(&cfg, DMA_SIZE_32);
            channel_config_set_read_increment(&cfg, false);
            channel_config_set_write_increment(&cfg, true);
            channel_config_set_dreq(&cfg, pio_get_dreq(FPGA_QSPI_PIO, FPGA_QSPI_PIO_SM, false));
            g_fpga_qspi.dma_rx_cfg = cfg;
        }
    }
}

bool fpga_selftest()
{
    // Test communication
    uint32_t result1, result2, result3;
    fpga_rdcmd(FPGA_CMD_COMMUNICATION_CHECK, (uint8_t*)&result1, 4);
    delay(1);
    fpga_rdcmd(FPGA_CMD_COMMUNICATION_CHECK, (uint8_t*)&result2, 4);
    delay(1);
    fpga_rdcmd(FPGA_CMD_COMMUNICATION_CHECK, (uint8_t*)&result3, 4);

    uint32_t expected = 0x03020100;
    if (result1 != expected || result2 != expected || result3 != expected)
    {
        logmsg("FPGA communication test failed, got ", result1, " ", result2, " ", result3, " expected ", expected);
        return false;
    }

    // Test register roundtrip
    uint32_t regs[2] = {0xBEEFBE80, 0xC0FEC0DE};
    uint32_t regs_readback[2] = {0,0};

    fpga_wrcmd(FPGA_CMD_WRITE_IDE_REGS, (uint8_t*)regs, 8);
    fpga_rdcmd(FPGA_CMD_READ_IDE_REGS, (uint8_t*)regs_readback, 8);

    if (regs[0] != regs_readback[0] || regs[1] != regs_readback[1])
    {
        logmsg("FPGA register roundtrip test failed, got ", regs_readback[0], " ", regs_readback[1]);
        return false;
    }

    // Test data buffer roundtrip
    uint32_t buf[4] = {0xA3A2A1A0, 0xB3B2B1B0, 0xC3C2C1C0, 0xD3D2D1D0};
    uint32_t buf_readback[4] = {0};
    uint16_t buflastidx = 7;
    fpga_wrcmd(FPGA_CMD_START_WRITE, (uint8_t*)&buflastidx, 2);
    fpga_wrcmd(FPGA_CMD_WRITE_DATABUF, (uint8_t*)buf, 16);
    fpga_rdcmd(FPGA_CMD_READ_DATABUF, (uint8_t*)buf_readback, 16);

    if (memcmp(buf, buf_readback, 16) != 0)
    {
        logmsg("FPGA databuf roundtrip test failed, got ", bytearray((uint8_t*)buf_readback, 16));
        return false;
    }

    return true;
}

bool fpga_init()
{
    // Enable clock output to FPGA
    // 15.6 MHz for now, resulting in FPGA clock of 60MHz.
    gpio_set_function(FPGA_CLK, GPIO_FUNC_GPCK);
    gpio_set_dir(FPGA_CLK, true);
    clock_gpio_init(FPGA_CLK, CLOCKS_CLK_GPOUT0_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS, 8);

    // Load bitstream
    if (!g_fpga_qspi.bitstream_loaded)
    {
        if (!fpga_load_bitstream())
        {
            logmsg("FPGA bitstream loading failed");
            return false;
        }

        g_fpga_qspi.bitstream_loaded = true;
    }
    
    // Set pins to QSPI mode
    fpga_io_as_qspi();
    fpga_qspi_pio_init();

    // Check protocol version
    uint8_t version;
    fpga_rdcmd(FPGA_CMD_PROTOCOL_VERSION, &version, 1);
    if (version != FPGA_PROTOCOL_VERSION)
    {
        logmsg("WARNING: FPGA reports protocol version ", (int)version, ", firmware is built for version ", (int)FPGA_PROTOCOL_VERSION);
    }

    if (!fpga_selftest())
    {
        logmsg("ERROR: FPGA self-test failed");
        return false;
    }

    // Check FPGA license code
    if (!g_fpga_qspi.license_done)
    {
        uint8_t license[17];
        fpga_rdcmd(0x7E, license, 17);
        logmsg("FPGA license code: ", bytearray(license + 1, 16));
        g_fpga_qspi.license_done = true;
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

void fpga_wrcmd(uint8_t cmd, const uint8_t *payload, size_t payload_len, uint16_t *crc)
{
    // Expecting a write-mode command
    assert(cmd & 0x80);

    // Start transfer and write command byte
    fpga_start_cmd(cmd);
    pio_sm_get_blocking(FPGA_QSPI_PIO, FPGA_QSPI_PIO_SM);

    if (payload_len == 0)
    {
        // Nothing to transmit
    }
    else if ((payload_len & 3) || ((uint32_t)payload & 3))
    {
        // Unaligned buffer
        pio_sm_put_blocking(FPGA_QSPI_PIO, FPGA_QSPI_PIO_SM, payload[0]);
        for (size_t i = 1; i < payload_len; i++)
        {
            pio_sm_put_blocking(FPGA_QSPI_PIO, FPGA_QSPI_PIO_SM, payload[i]);
            pio_sm_get_blocking(FPGA_QSPI_PIO, FPGA_QSPI_PIO_SM);
        }
        pio_sm_get_blocking(FPGA_QSPI_PIO, FPGA_QSPI_PIO_SM);
    }
    else
    {
        // Configure in 32-bit mode for data transfer
        pio_sm_set_enabled(FPGA_QSPI_PIO, FPGA_QSPI_PIO_SM, false);
        pio_sm_init(FPGA_QSPI_PIO, FPGA_QSPI_PIO_SM,
                g_fpga_qspi.pio_offset_qspi_transfer,
                &g_fpga_qspi.pio_cfg_qspi_transfer_32bit);
        pio_sm_set_enabled(FPGA_QSPI_PIO, FPGA_QSPI_PIO_SM, true);

        // Transmit 32 bits at a time using DMA
        uint32_t num_words = payload_len / 4;
        uint32_t dummy = 0;
        dma_channel_config cfg_dummy_rx = g_fpga_qspi.dma_rx_cfg;
        channel_config_set_write_increment(&cfg_dummy_rx, false);
        dma_channel_configure(FPGA_QSPI_DMA_RX,
            &cfg_dummy_rx, &dummy, &FPGA_QSPI_PIO->rxf[FPGA_QSPI_PIO_SM],
            num_words, true);

        dma_hw->sniff_data = 0x4ABA; // ATA CRC16 initialization value
        dma_channel_configure(FPGA_QSPI_DMA_TX,
            &g_fpga_qspi.dma_tx_cfg, &FPGA_QSPI_PIO->txf[FPGA_QSPI_PIO_SM], payload,
            num_words, false);
        dma_sniffer_enable(FPGA_QSPI_DMA_TX, 0x03, true);
        dma_channel_start(FPGA_QSPI_DMA_TX);

        uint32_t start = millis();
        while (dma_channel_is_busy(FPGA_QSPI_DMA_RX))
        {
            if ((uint32_t)(millis() - start) > 100)
            {
                logmsg("fpga_wrcmd() DMA timeout, ctrl:", dma_hw->ch[FPGA_QSPI_DMA_RX].al1_ctrl, " payload_len: ", (int)payload_len);
                break;
            }
        }

        dma_channel_abort(FPGA_QSPI_DMA_RX);
        dma_channel_abort(FPGA_QSPI_DMA_TX);
        dma_sniffer_disable();

        if (crc) *crc = dma_hw->sniff_data;
    }

    fpga_release();
}

void fpga_rdcmd(uint8_t cmd, uint8_t *result, size_t result_len, uint16_t *crc)
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

    if (result_len == 0)
    {
        // Nothing to receive
    }
    else if ((result_len & 3) || ((uint32_t)result & 3))
    {
        // Unaligned buffer
        pio_sm_put_blocking(FPGA_QSPI_PIO, FPGA_QSPI_PIO_SM, 0xFF);
        for (size_t i = 0; i < result_len - 1; i++)
        {
            pio_sm_put_blocking(FPGA_QSPI_PIO, FPGA_QSPI_PIO_SM, 0xFF);
            result[i] = pio_sm_get_blocking(FPGA_QSPI_PIO, FPGA_QSPI_PIO_SM) >> 24;
        }
        result[result_len - 1] = pio_sm_get_blocking(FPGA_QSPI_PIO, FPGA_QSPI_PIO_SM) >> 24;
    }
    else
    {
        // Configure in 32-bit mode for data transfer
        pio_sm_set_enabled(FPGA_QSPI_PIO, FPGA_QSPI_PIO_SM, false);
        pio_sm_init(FPGA_QSPI_PIO, FPGA_QSPI_PIO_SM,
                g_fpga_qspi.pio_offset_qspi_transfer,
                &g_fpga_qspi.pio_cfg_qspi_transfer_32bit);
        pio_sm_set_enabled(FPGA_QSPI_PIO, FPGA_QSPI_PIO_SM, true);

        // Receive 32 bits at a time using DMA
        uint32_t num_words = result_len / 4;
        dma_hw->sniff_data = 0x4ABA; // ATA CRC16 initialization value
        dma_channel_configure(FPGA_QSPI_DMA_RX,
            &g_fpga_qspi.dma_rx_cfg, result, &FPGA_QSPI_PIO->rxf[FPGA_QSPI_PIO_SM],
            num_words, false);
        dma_sniffer_enable(FPGA_QSPI_DMA_RX, 0x03, true);
        dma_channel_start(FPGA_QSPI_DMA_RX);

        uint32_t dummy = 0;
        dma_channel_config cfg_dummy_tx = g_fpga_qspi.dma_tx_cfg;
        channel_config_set_read_increment(&cfg_dummy_tx, false);
        dma_channel_configure(FPGA_QSPI_DMA_TX,
            &g_fpga_qspi.dma_tx_cfg, &FPGA_QSPI_PIO->txf[FPGA_QSPI_PIO_SM], &dummy,
            num_words, true);

        uint32_t start = millis();
        while (dma_channel_is_busy(FPGA_QSPI_DMA_RX))
        {
            if ((uint32_t)(millis() - start) > 100)
            {
                logmsg("fpga_rdcmd() DMA timeout, ctrl:", dma_hw->ch[FPGA_QSPI_DMA_RX].al1_ctrl, " result_len: ", (int)result_len);
                break;
            }
        }

        dma_channel_abort(FPGA_QSPI_DMA_RX);
        dma_channel_abort(FPGA_QSPI_DMA_TX);
        dma_sniffer_disable();

        if (crc) *crc = dma_hw->sniff_data;
    }

    fpga_release();
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
