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

// GPIO definitions for ZuluIDE RP2040-based hardware

#pragma once

#include <hardware/gpio.h>

// FPGA configuration pins
#define FPGA_CRESET 0
#define FPGA_CDONE 7
#define FPGA_SS 1
#define FPGA_SCK 2
#define FPGA_MOSI 3
#define FPGA_MISO 4
#define FPGA_SPI spi0

// Clock output to FPGA
#define FPGA_CLK 25

// QSPI bus to FPGA
// Overlaps the configuration SPI bus
#define FPGA_QSPI_SS 1
#define FPGA_QSPI_SCK 2
#define FPGA_QSPI_D0 3
#define FPGA_QSPI_D1 4
#define FPGA_QSPI_D2 5
#define FPGA_QSPI_D3 6

// IDE initialization status signals
#define IDE_CSEL_IN 8
#define IDE_PDIAG_IN 9
#define IDE_DASP_IN 10

// SD card pins in SDIO mode
#define SDIO_CLK 18
#define SDIO_CMD 19
#define SDIO_D0  20
#define SDIO_D1  21
#define SDIO_D2  22
#define SDIO_D3  23

// Expansion I2C bus
#define GPIO_I2C_SDA 26
#define GPIO_I2C_SCL 27
#define GPIO_EXT_INTERRUPT 15

#ifdef ENABLE_AUDIO_OUTPUT
    // i2s PIO settings
    #define I2S_PIO_HW pio0_hw
    #define I2S_PIO_SM 1
    // 44.1KHz to the nearest integer with a sys clk of 135.43MHz and 2 x 16-bit samples with the pio clock running 2x I2S clock
    // 135.43Mhz / 16 / 2 / 2 / 44.1KHz = 47.98 ~= 48
    #define I2S_PIO_DIVIDER 48
    // audio subsystem DMA channels
    #define SOUND_DMA_CHA 6
    #define SOUND_DMA_CHB 7
    // IO expander I2S pins being used as SPI for audio
    #define GPIO_I2S_BCLK  12
    #define GPIO_I2S_LRCLK 13
    #define GPIO_I2S_DOUT  14
#endif

// GPIO Expansion bus pins IO0-IO7
#define EXP_ROT_A_PIN   0
#define EXP_ROT_B_PIN   1
#define EXP_EJECT_PIN   2
#define EXP_ROT_PIN     5
#define EXP_INSERT_PIN  3

// DIP switch pins
#define DIP_CABLESEL 29
#define DIP_DRIVE_ID 28
#define DIP_DBGLOG 24

// Serial output pin
#define SWO_PIN 16

// Status LED
#define STATUS_LED 17


// GPIO Eject buttons
#define GPIO_EJECT_BTN_1_PIN 11
#define GPIO_EJECT_BTN_2_PIN 14