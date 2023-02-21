// GPIO definitions for ZuluIDE RP2040-based hardware

#pragma once

#include <hardware/gpio.h>

// IDE data port on pins 0-15
// Used for data in/out and for controlling registers.
#define IDE_IO_SHIFT 0
#define IDE_IO_MASK 0xFFFF

// The hardware uses a multiplexed 16-bit data bus to access the signals.
// The signals accessed are selected primarily by MUX_SEL and secondarily
// by bits written to mux control register on rising edge of MUX_SEL.
//
// MUX_SEL   CR_DATA_SEL   CR_DATA_DIR    Data bus contents
// -------------------------------------------------------------
//    0          x              x         Data to load to mux control register
//                                        on next rising edge of MUX_SEL (bits CR_xxxx).
//    1          0              x         IDE status signals (bits SI_xxxx).
//    1          1              0         Data input from IDE bus
//    1          1              1         Data output to IDE bus

// Multiplexer select signal
// Rising edge = load control register
// High = enable data registers
#define MUX_SEL 16

// Bits accessed through the mux control register
// The ones marked _NEG_ are active low.
#define CR_MUX_OE           0
#define CR_DATA_SEL         1
#define CR_DATA_DIR         2
#define CR_NEG_CTRL_OUT     3
#define CR_NEG_CTRL_IN      4
#define CR_STATUS_LED       7
#define CR_IDE_DMARQ        8
#define CR_NEG_IDE_IOCS16   10
#define CR_NEG_IDE_PDIAG    11
#define CR_NEG_IDE_DASP     12
#define CR_IDE_INTRQ        13
#define CR_NEG_IDE_INTRQ_EN 14
#define CR_IDLE_VALUE ((1 << CR_MUX_OE) | \
                       (1 << CR_NEG_CTRL_OUT) | \
                       (1 << CR_NEG_CTRL_IN) | \
                       (1 << CR_NEG_IDE_IOCS16) | \
                       (1 << CR_NEG_IDE_PDIAG) | \
                       (1 << CR_NEG_IDE_DASP) | \
                       (1 << CR_NEG_IDE_INTRQ_EN))

// Status signals accessed through input multiplexed
#define SI_DA0      0
#define SI_DA1      1
#define SI_DA2      2
#define SI_CS0      3
#define SI_CS1      4
#define SI_CSEL     5
#define SI_PDIAG    6
#define SI_DMACK    7

// Strobe signals
#define IDE_OUT_IORDY 17
#define IDE_IN_DIOR 24
#define IDE_IN_DIOW 25
#define IDE_IN_RST 29

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
#define DIP_CABLESEL 24
#define DIP_DRIVE_ID 25
#define DIP_DBGLOG 28

// Serial output pin
#define SWO_PIN 28
