/**
 * ZuluIDE™ platform support - Copyright (c) 2025 Rabbit Hole Computing™
 *
 * API interface between Core 0 GPLv3+exception code and the proprietary Core1
 * hardware support library.
 */

#pragma once
#include <stdint.h>
#include <stddef.h>
#include <ide_phy.h>
#include <pico/multicore.h>
#include <string.h>

// Events are reported by core1 in g_idecomm.events
#define CORE1_EVT_CMD_RECEIVED      0x0001
#define CORE1_EVT_HWRST             0x0002
#define CORE1_EVT_SWRST             0x0004
#define CORE1_EVT_DATA_DONE         0x0008

// Requests are written by core0 to g_idecomm.requests
#define CORE1_REQ_SET_REGS          0x0001
#define CORE1_REQ_ASSERT_IRQ        0x0002
#define CORE1_REQ_START_DATAIN      0x0004
#define CORE1_REQ_START_DATAOUT     0x0008
#define CORE1_REQ_STOP_TRANSFERS    0x0010
#define CORE1_REQ_PRINT_DEBUG       0x0020
#define CORE1_REQ_SET_SIGNALS       0x0040
#define CORE1_REQ_GET_SIGNALS       0x0080
#define CORE1_REQ_CHANGE_PIO_MODE   0x0100

// This is set and cleared by core1 to indicate whether
// it is busy handling previous requests.
#define CORE1_REQ_BUSY              0x80000000

// If the CORE1_REQ_BUSY mechanism is not appropriate,
// CORE0 can instead wait for CORE1_RESPONSE_DELAY microseconds
// after posting a CORE1 request.
#ifndef CORE1_RESPONSE_DELAY
#define CORE1_RESPONSE_DELAY 100
#endif

typedef struct {
    ide_registers_t regs;
    int state_irqreq: 1; // Interrupt is being asserted to host
    int state_datain: 1; // Data transfer from IDE is in progress
    int state_dataout: 1; // Data transfer to IDE is in progress
    int state_reserved: 13;
} phy_ide_registers_t;

static_assert(sizeof(phy_ide_registers_t) == 12);

extern struct idecomm_t {
    phy_ide_registers_t phyregs; // Latest value of IDE registers from core1

    char core1_log[1024]; // Log messages from core1
    uint32_t logpos;

    bool enable_idephy; // Enable core1 code to run

    uint32_t datablocksize; // Data block size for current data transfer
    
    // Configuration
    bool enable_dev0; // Answer to register reads for device 0 with actual data
    bool enable_dev1; // Answer to register reads for device 1 with actual data
    bool enable_dev1_zeros; // Answer to register reads for device 1 with zeros
    bool atapi_dev0; // Automatically read command for ATAPI PACKET on device 0
    bool atapi_dev1; // Automatically read command for ATAPI PACKET on device 1
    bool disable_iordy; // Disable IORDY in PIO mode 

   // Enables INTRQ between the initial ATA PACKET command and receiving the ATAPI command
    bool enable_packet_intrq;

    // IOCS16 signaling for PIO data transfer implementation is not completely to spec on the ZuluIDE V2
    // IOCS16 is being activated for the whole data transfer, instead of based on register address.
    // This only matters for systems where ATA interface is directly on ISA bus (ATA v2 implementation)
    // Most of such systems are ok with the current workaround, but if necessary, it can be disabled.
    bool disable_iocs16;

    // Event flags set by core1, cleared by core0
    uint32_t events;

    // Request flags set by core0, cleared by core1
    // Core1 should be waken up by setting IRQ7 in PIO0.
    uint32_t requests;

    // UDMA mode must be set before START_DATAIN/DATAOUT request and
    // not changed until transfer is done.
    // If set to -1, PIO data transfer is used.
    int udma_mode;

    // Number of UDMA checksum errors
    int udma_checksum_errors;

    // CPU clock rate
    // This is needed for UDMA timing setup and is not directly available for
    // the code running in different security context.
    int cpu_freq_hz;

    // Slow diagnostic signals (IDE_SIGNAL_DASP, IDE_SIGNAL_PDIAG).
    // Access with CORE1_REQ_SET_SIGNALS and CORE1_REQ_GET_SIGNALS
    uint8_t set_signals;
    uint8_t get_signals;

    // Active PIO mode.
    int pio_mode;
} g_idecomm;

#define IDE_PIO                pio0
#define IDE_CORE1_WAKEUP_IRQ   7

// DMA channels and interrupts reserved for core 1
#define IDE_PHY_DMACH_A     2
#define IDE_PHY_DMACH_B     3
#define IDE_PHY_DMACH_C     4
#define IDE_PHY_DMAIRQ      3

// Data buffers are transferred as pointers over the inter-core FIFO.
// The data needs to be padded to 32 bit words, of which 16 bits are the payload.
// Top bits must be set to IDECOMM_DATA_PATTERN
#define IDECOMM_MAX_BLOCKSIZE 8192
#define IDECOMM_MAX_BLOCK_PAYLOAD 4096
#define IDECOMM_BUFFERCOUNT 8
#define IDECOMM_DATA_PATTERN 0x80060000
#define IDECOMM_DATAFORMAT_PIO(x) ((x) | IDECOMM_DATA_PATTERN)
extern uint8_t g_idebuffers[IDECOMM_BUFFERCOUNT][IDECOMM_MAX_BLOCKSIZE];

void zuluide_rp2350b_core1_run();
