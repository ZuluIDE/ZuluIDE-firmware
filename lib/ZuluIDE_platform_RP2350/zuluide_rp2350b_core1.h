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
#define CORE1_REQ_SET_REGS              0x0001
#define CORE1_REQ_ASSERT_IRQ            0x0002
#define CORE1_REQ_START_DATAIN          0x0004
#define CORE1_REQ_START_DATAOUT         0x0008
#define CORE1_REQ_STOP_TRANSFERS        0x0010
#define CORE1_REQ_PRINT_DEBUG           0x0020
#define CORE1_REQ_SET_SIGNALS           0x0040
#define CORE1_REQ_GET_SIGNALS           0x0080
#define CORE1_REQ_CHANGE_PIO_MODE       0x0100
#define CORE1_REQ_START_DATAOUT_NOIRQ   0x0200
#define CORE1_REQ_SET_REGS_DEV0         0x0400
#define CORE1_REQ_SET_REGS_DEV1         0x0800
// Request core1 (secure mode) to call reset_usb_boot(0,0) on behalf of
// core0 which runs in ARM Non-Secure mode and cannot call it directly.
#define CORE1_REQ_RESET_UF2             0x1000

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
    ide_registers_t set_regs; // Values to set on SET_REGS request

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

    // For IOCS16 handling, the IDE register address is transferred to a state machine in
    // another PIO block. This is done because address pins are in GPIO6-10 while IOCS16
    // is on GPIO32. On RP2350 the same PIO cannot access them both.
    // This config must be set before setting enable_idephy.
    bool enable_iocs16;
    int iocs16_dma_channel;
    uint32_t iocs16_dma_target_addr;

    // Event flags set by core1, cleared by core0
    uint32_t events;

    // Request flags set by core0, cleared by core1
    // Core1 should be waken up by setting IRQ7 in PIO0.
    uint32_t requests;

    // UDMA mode must be set before START_DATAIN/DATAOUT request and
    // not changed until transfer is done.
    // If set to -1, Multiword DMA (see mwdma_mode) or PIO data transfer is used.
    int udma_mode;

    // Number of UDMA checksum errors, also counts Multiword DMA transfer errors
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

    // ATA PIO data-in sector tracking (e.g. READ SECTORS).
    // Set by core0 before the first block of a PIO data-in transfer, 0 disables tracking.
    // After each block core1 decrements the sector count register by datain_sectors_per_block,
    // and after the last block sets status to DRDY|DSC instead of BSY.
    uint32_t datain_sectors_remaining;
    uint32_t datain_sectors_per_block;

    // Command sequence number, incremented by core1 when host writes the COMMAND
    // register and on software or hardware reset.
    uint32_t cmd_seq;

    // Command sequence number that the CORE1_REQ_SET_REGS, CORE1_REQ_ASSERT_IRQ,
    // CORE1_REQ_STOP_TRANSFERS and CORE1_REQ_START_* requests belong to, set by core0
    // before posting them.
    // Core1 drops the requests if the host has since started a new command or reset.
    // SET_REGS and ASSERT_IRQ are also dropped if core1 has already completed the command
    // itself (last sector of a tracked PIO data-in transfer).
    // IDECOMM_SEQ_ANY marks requests that are not tied to a host command (PHY config and
    // reset), these are never dropped.
    uint32_t set_regs_seq;

    // Multiword DMA mode, used when udma_mode is -1. Like udma_mode, it must be set before
    // START_DATAIN/DATAOUT request and not changed until transfer is done.
    // Data blocks are in the PIO data format, see IDECOMM_DATAFORMAT_PIO().
    // If set to -1, PIO data transfer is used.
    int mwdma_mode;
} g_idecomm;

// Value of set_regs_seq for requests that must be applied regardless of the command sequence.
// Core1 never uses this value for cmd_seq.
#define IDECOMM_SEQ_ANY 0xFFFFFFFFu

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
