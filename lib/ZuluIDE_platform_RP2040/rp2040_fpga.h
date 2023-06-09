// Access to external ICE5LP1K FPGA that is used to implement
// IDE bus communications.
//
// The communication is formatted into packets, which begin when SS select line goes low.
// Data bytes are transferred as 4-bit nibbles, LSB first.
// Low nibble is written on falling edge and read on rising edge.
// High nibble is written on rising edge and read on falling edge.
// Transfer direction depends on command.
//
//  SS  ^^^^^^|________________|^^^^^
// SCK  xxxxxx__|^|_|^|_|^|_|^|_______
//  SD         C C 0 0 1 1 2 2     C = command, 0..2 = data payload
//
// The highest bit (SD3 on second cycle) of command byte determines payload transfer
// direction. High level means transfer to FPGA.
//
// Packet format when writing to FPGA:
//   Byte 0:   Command byte, always from microcontroller to FPGA.
//             Bit 7 determines data direction, 1 = transfer to FPGA.
//   Byte 1-N: Command payload.
//
// Packet format when reading from FPGA:
//   Byte 0:   Command byte, always from microcontroller to FPGA.
//             Bit 7 determines data direction, 0 = transfer from FPGA.
//   Byte 1:   Bus turnaround time, discard data.
//             CPU should set data bus to input when it sends falling edge of command byte
//             FPGA will set data bus to output when it receives falling edge of turnaround byte
//   Byte 2-N: Response payload
//
// Commands:
//     0x00:    Read status, response:
//                 Byte 0:   FPGA status
//                             Bit 0:  Data FIFO direction, 1 = write to IDE
//                                 1:  Data buffer ready for reading
//                                 2:  Data buffer ready for writing
//                                 3:  Host has read all buffered data
//                                 4:  IDE bus reset has occurred (use 0x92 to clear)
//                                 5:  IDE DEVICE_CONTROL software reset has occurred (use 0x92 to clear)
//                                 6:  IDE command register has been written (use 0x92 to clear)
//                                 7:  Any IDE register has been written (use 0x92 to clear)
//                 Byte 1:   IDE status register
//
//     0x01:    Read IDE registers, response:
//                 Byte 0:   IDE STATUS
//                 Byte 1:   COMMAND
//                 Byte 2:   DEVICE
//                 Byte 3:   DEVICE_CONTROL
//                 Byte 4:   ERROR
//                 Byte 5:   FEATURE
//                 Byte 6:   SECTOR_COUNT
//                 Byte 7:   LBA_LOW
//                 Byte 8:   LBA_MID
//                 Byte 9:   LBA_HIGH
//
//     0x02:    Read received data block from buffer (length set by 0x83)
//                 Byte 0-N: Data bytes received from IDE host
//
//     0x10:    Read latest 256 bytes of IDE register trace
//                 Byte 0: Trace buffer write counter index (can be used to determine number of new bytes)
//                 Byte 1-256: Newest trace data
//
//     0x7D:    Read QSPI protocol version
//                 Byte 0: QSPI protocol version (1-255)
//
//     0x7E:    Read license check status
//                 Byte 0: 0x00: License check failed
//                         0x01 to 0x10: License check passed
//                         0xFF: License check in progress
//                 Bytes 1-N: License request code
//
//     0x7F:    QSPI Communication check, response:
//                 Byte 0-255: Values 0-255
//
//     0x80:    Set IDE PHY configuration, payload:
//                 Byte 0: Device selection
//                           Bit 0: Enable device 0 register reads
//                               1: Enable device 1 register reads
//                               2: Enable answering device 1 with zeros (ATAPI)
//                               3: Enable ATAPI PACKET handling for device 0
//                               4: Enable ATAPI PACKET handling for device 1
//
//     0x81:    Write IDE registers, payload:
//                 Byte 0:   IDE STATUS
//                 Byte 1:   COMMAND
//                 Byte 2:   DEVICE
//                 Byte 3:   DEVICE_CONTROL
//                 Byte 4:   ERROR
//                 Byte 5:   FEATURE
//                 Byte 6:   SECTOR_COUNT
//                 Byte 7:   LBA_LOW
//                 Byte 8:   LBA_MID
//                 Byte 9:   LBA_HIGH
//
//     0x82:   Configure data buffer for write to IDE bus, payload:
//                 Byte 0-1: Block length minus 1 (bytes)
//
//     0x83:   Configure data buffer for read from IDE bus, payload:
//                 Byte 0-1: Block length minus 1 (bytes)
//
//     0x84:   Write to IDE data buffer (length set by 0x82):
//                 Byte 0-N: Data to transmit
//
//     0x90:   Write IDE diagnostic signals, payload:
//                 Byte 0:  Bit 0: OUT_DASP    drive present / active
//                              1: OUT_PDIAG   passed diagnostics
//
//     0x91:   Assert IDE interrupt (if enabled and selected) and write status register simultaneously
//               Byte 0: IDE STATUS
//
//     0x92:   Clear FPGA status interrupt flags
//                 Byte 0:   Mask of flags to clear
//                             Bit 0-3: Don't care
//                                 4:  Clear "IDE bus reset has occurred since last status read"
//                                 5:  Clear "IDE DEVICE_CONTROL software reset has occurred"
//                                 6:  Clear "IDE command register has been written"
//                                 7:  Clear "Any IDE register has been written"

#pragma once
#include <stdint.h>
#include <stdlib.h>

// Initialize FPGA and load bitstream
bool fpga_init();

// Send a write command to FPGA through QSPI bus
// If keep_active is true, payload can be transferred using fpga_wrdata_start().
void fpga_wrcmd(uint8_t cmd, const uint8_t *payload, size_t payload_len, bool keep_active = false);

// Send a read command to FPGA through QSPI bus
// If keep_active is true, payload can be transferred using fpga_rddata_start().
void fpga_rdcmd(uint8_t cmd, uint8_t *result, size_t result_len, bool keep_active = false);

// Start a data transfer to/from FPGA using DMA
void fpga_wrdata_start(const uint32_t *data, size_t num_words);
void fpga_rddata_start(uint32_t *data, size_t num_words);

// Wait for DMA data transfer to finish
void fpga_transfer_finish();

// Dump IDE register values
void fpga_dump_ide_regs();

#define FPGA_PROTOCOL_VERSION 2

#define FPGA_CMD_READ_STATUS            0x00
#define FPGA_CMD_READ_IDE_REGS          0x01
#define FPGA_CMD_READ_DATABUF           0x02
#define FPGA_CMD_READ_TRACEBUF          0x10
#define FPGA_CMD_PROTOCOL_VERSION       0x7D
#define FPGA_CMD_LICENSE_CHECK          0x7E
#define FPGA_CMD_COMMUNICATION_CHECK    0x7F
#define FPGA_CMD_SET_IDE_PHY_CFG        0x80
#define FPGA_CMD_WRITE_IDE_REGS         0x81
#define FPGA_CMD_START_WRITE            0x82
#define FPGA_CMD_START_READ             0x83
#define FPGA_CMD_WRITE_DATABUF          0x84
#define FPGA_CMD_WRITE_IDE_SIGNALS      0x90
#define FPGA_CMD_ASSERT_IRQ             0x91
#define FPGA_CMD_CLR_IRQ_FLAGS          0x92

#define FPGA_STATUS_DATA_DIR            0x01
#define FPGA_STATUS_RX_DONE             0x02
#define FPGA_STATUS_TX_CANWRITE         0x04
#define FPGA_STATUS_TX_DONE             0x08
#define FPGA_STATUS_IDE_RST             0x10
#define FPGA_STATUS_IDE_SRST            0x20
#define FPGA_STATUS_IDE_CMD             0x40
#define FPGA_STATUS_IDE_WR              0x80
