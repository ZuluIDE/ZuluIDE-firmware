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
 * Under Section 7 of GPL version 3, you are granted additional
 * permissions described in the ZuluIDE Hardware Support Library Exception
 * (GPL-3.0_HSL_Exception.md), as published by Rabbit Hole Computing™.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
**/

// GPIO definitions for ZuluIDE RP2350-based hardware

#pragma once

#include <hardware/gpio.h>

// Control signal multiplexer
#define CTRL_LOAD       0
#define CTRL_nEN        1
#define CTRL_IN_SEL     2
#define IDE_RST         3
#define IDE_DIOW        4
#define IDE_DIOR        5
#define IDE_DA0         6
#define IDE_DA1         7
#define IDE_DA2         8
#define IDE_CS0         9
#define IDE_CS1         10
#define IDE_DMACK       11
#define IDE_D0          12
#define IDE_D1          13
#define IDE_D2          14
#define IDE_D3          15
#define IDE_D4          16
#define IDE_D5          17
#define IDE_D6          18
#define IDE_D7          19
#define IDE_D8          20
#define IDE_D9          21
#define IDE_D10         22
#define IDE_D11         23
#define IDE_D12         24
#define IDE_D13         25
#define IDE_D14         26
#define IDE_D15         27
#define IDE_DATASEL     28
#define IDE_DATADIR     29
#define IDE_IORDY_OUT   30
#define IDE_IORDY_EN    31
#define IDE_IOCS16      32

// SD card pins in SDIO mode
// PIO GPIOBASE will be set to 16
#define SDIO_GPIOBASE 16
#define SDIO_CLK 34
#define SDIO_CMD 35
#define SDIO_D0  36
#define SDIO_D1  37
#define SDIO_D2  38
#define SDIO_D3  39

// Expansion I2C bus
#define GPIO_I2C_SDA 44
#define GPIO_I2C_SCL 45
#define GPIO_EXT_INTERRUPT 47

#ifdef ENABLE_AUDIO_OUTPUT
    // IO expander I2C pins being used as SPI for audio
    #define GPIO_I2S_BCLK  40
    #define GPIO_I2S_LRCLK 41
    #define GPIO_I2S_DOUT  42
#endif

// GPIO Expansion bus pins IO0-IO7
#define EXP_ROT_A_PIN   0
#define EXP_ROT_B_PIN   1
#define EXP_EJECT_PIN   2
#define EXP_ROT_PIN     5
#define EXP_INSERT_PIN  3

// DIP switch pins
// Available when CTRL_IN_SEL = 0
#define DIP_CABLESEL IDE_D10
#define DIP_DRIVE_ID IDE_D11
#define DIP_DBGLOG   IDE_D12
#define IDE_CABLESEL IDE_D15

// Serial output pin
#define SWO_PIN 46

// Status LED
#define STATUS_LED 33

// GPIO Eject buttons
#define GPIO_EJECT_BTN_1_PIN 43
#define GPIO_EJECT_BTN_2_PIN GPIO_EXT_INTERRUPT
