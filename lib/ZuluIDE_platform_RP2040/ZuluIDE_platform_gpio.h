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

// DIP switch pins
#define DIP_CABLESEL 29
#define DIP_DRIVE_ID 28
#define DIP_DBGLOG 24

// Serial output pin
#define SWO_PIN 16

// Status LED
#define STATUS_LED 17
