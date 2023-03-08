// This file implements interface to IDE / Parallel ATA bus.
// It runs on the second core of RP2040.
//
// To avoid memory contention with other code, the separate
// RAM blocks scratch X and scratch Y are used as follows:
// Scratch X: Data accessed by hardware DMA
// Scratch Y: Data accessed by second core

#include "ZuluIDE_platform.h"
#include "ZuluIDE_config.h"
#include "ZuluIDE_log.h"
#include <hardware/gpio.h>
#include <hardware/pio.h>
#include <hardware/dma.h>
#include <hardware/structs/iobank0.h>
#include <multicore.h>
#include <assert.h>
#include <minIni.h>
#include "ide_constants.h"
#include "ide_phy.h"
#include "ide_phy.pio.h"

// Helper macro to construct bit mask from bit index
#define BM(x) (1UL << (x))

// Helper macro to reverse bits in 16-bit word
// This is used because the bit reverse MOV is fastest
// way to access high-order word in PIO code.
#define REVBITS(x) ( \
    (((x) & BM( 0)) ? BM(15) : 0) | \
    (((x) & BM( 1)) ? BM(14) : 0) | \
    (((x) & BM( 2)) ? BM(13) : 0) | \
    (((x) & BM( 3)) ? BM(12) : 0) | \
    (((x) & BM( 4)) ? BM(11) : 0) | \
    (((x) & BM( 5)) ? BM(10) : 0) | \
    (((x) & BM( 6)) ? BM( 9) : 0) | \
    (((x) & BM( 7)) ? BM( 8) : 0) | \
    (((x) & BM( 8)) ? BM( 7) : 0) | \
    (((x) & BM( 9)) ? BM( 6) : 0) | \
    (((x) & BM(10)) ? BM( 5) : 0) | \
    (((x) & BM(11)) ? BM( 4) : 0) | \
    (((x) & BM(12)) ? BM( 3) : 0) | \
    (((x) & BM(13)) ? BM( 2) : 0) | \
    (((x) & BM(14)) ? BM( 1) : 0) | \
    (((x) & BM(15)) ? BM( 0) : 0) \
)

// Register addresses
// These are in the order of bits 0-7 read from the status signal input multiplexer.
// Refer to tables 15 and 16 starting on page 58 in "T13/1410D revision 3a"
// The CS bits are defined with inverted polarity in the spec.
//
// IDE register quick reference:
// 0x0E RD: ALT_STATUS          Same as status, does not clear IRQ
//      WR: DEVICE_CONTROL      7: HOB, 2: SRST, 1: nIEN
// 0x10 RW: DATA
// 0x11 RD: ERROR               2: ABORT
//      WR: FEATURE             Command parameter
// 0x12 RW: SECTOR_COUNT
// 0x13 RW: LBA_LOW
// 0x14 RW: LBA_MID
// 0x15 RW: LBA_HIGH
// 0x16 RW: DEVICE              4: DEV
// 0x17 RD: STATUS              7: BSY, 6: DRDY, 5: DF, 3: DRQ, 0: ERR
//      WR: COMMAND
//

#define REGADDR(ncs0, ncs1, da2, da1, da0) \
    ((ncs0 ? 0 : BM(SI_CS0)) | \
     (ncs1 ? 0 : BM(SI_CS1)) | \
     (da2  ? BM(SI_DA2) : 0) | \
     (da1  ? BM(SI_DA1) : 0) | \
     (da0  ? BM(SI_DA0) : 0))

#define REGADDR_MASK (BM(SI_CS0) | BM(SI_CS1) | BM(SI_DA2) | BM(SI_DA1) | BM(SI_DA0))

// Write requests
#define REGWR_DEVICE_CONTROL    REGADDR(0,1,1,1,0)
#define REGWR_DATA              REGADDR(1,0,0,0,0)
#define REGWR_FEATURE           REGADDR(1,0,0,0,1)
#define REGWR_SECTORCOUNT       REGADDR(1,0,0,1,0)
#define REGWR_LBALOW            REGADDR(1,0,0,1,1)
#define REGWR_LBAMID            REGADDR(1,0,1,0,0)
#define REGWR_LBAHIGH           REGADDR(1,0,1,0,1)
#define REGWR_DEVICE            REGADDR(1,0,1,1,0)
#define REGWR_COMMAND           REGADDR(1,0,1,1,1)

// Read requests
#define REGRD_ALT_STATUS        REGADDR(0,1,1,1,0)
#define REGRD_DATA              REGADDR(1,0,0,0,0)
#define REGRD_ERROR             REGADDR(1,0,0,0,1)
#define REGRD_SECTORCOUNT       REGADDR(1,0,0,1,0)
#define REGRD_LBALOW            REGADDR(1,0,0,1,1)
#define REGRD_LBAMID            REGADDR(1,0,1,0,0)
#define REGRD_LBAHIGH           REGADDR(1,0,1,0,1)
#define REGRD_DEVICE            REGADDR(1,0,1,1,0)
#define REGRD_STATUS            REGADDR(1,0,1,1,1)

// Default MUX control value for monitoring status signals
#define MUXCR_STATUS \
     (BM(CR_MUX_OE) | \
      BM(CR_NEG_CTRL_OUT) | \
      BM(CR_NEG_IDE_IOCS16) | \
      BM(CR_NEG_IDE_PDIAG) | \
      BM(CR_NEG_IDE_DASP) | \
      BM(CR_NEG_IDE_INTRQ_EN))

#define IDE_PIO pio0
#define IDE_PIO_SM_CRLOAD 0
#define IDE_PIO_SM_REGWR 1
#define IDE_PIO_SM_REGRD 2

// Register reads and writes have very short response times.
// As such they are executed by PIO and DMA hardware.
//
// For register writes the chain goes:
// 1. IDE_PIO_SM_REGWR generates lookup address to g_ide_register_wr_lookup
// 2. IDE_DMACH_REGWR1 copies lookup address to IDE_DMACH_REGWR2 source address
// 3. IDE_DMACH_REGWR2 copies value from lookup table to IDE_DMACH_REGWR3 destination address
// 4. IDE_DMACH_REGWR3 copies data from IDE_PIO_SM_REGWR to memory
//
// For register reads:
// 1. IDE_PIO_SM_REGRD generates address to g_ide_registers
// 2. IDE_DMACH_REGRD1 copies address to IDE_DMACH_REGRD2 source address
// 3. IDE_DMACH_REGRD2 copies data to IDE_PIO_SM_REGRD
#define IDE_DMACH_REGWR1 2
#define IDE_DMACH_REGWR2 3
#define IDE_DMACH_REGWR3 4
#define IDE_DMACH_REGRD1 5
#define IDE_DMACH_REGRD2 6

// Register set that can be read and possibly written by host
// Addressed by REGRD_ addresses
__attribute__((section(".scratch_x.idepio"), aligned(32 * 2)))
static volatile uint16_t g_ide_registers[32];

// Lookup table to get the address for register writes
// Addressed by REGWR_ addresses
__attribute__((section(".scratch_x.idepio"), aligned(32 * 4)))
static volatile uint16_t *g_ide_register_wr_lookup[32];

__attribute__((section(".scratch_x.idepio")))
static struct {
    // Write-only registers
    // Only bottom 8 bits of each is significant.
    uint16_t command;
    uint16_t feature;
    uint16_t device_control;
    uint16_t invalid_addr;

    // Which devices to emulate
    bool device0;
    bool device1;

    // Latest mux control register value loaded to PIO
    uint32_t control_reg;

    // Low level tracing
    bool lowlevel_trace;

    // LED state
    volatile bool status_led;

    bool channels_claimed;
    uint32_t pio_offset_ide_cr_loader;
    pio_sm_config pio_cfg_ide_cr_loader;
    uint32_t pio_offset_ide_reg_write;
    pio_sm_config pio_cfg_ide_reg_write;
    uint32_t pio_offset_ide_reg_read;
    pio_sm_config pio_cfg_ide_reg_read;
} g_ide_phy;

__attribute__((section(".scratch_y.idephy_stack")))
static uint32_t g_ide_phy_core1_stack[512];

/**************************************/
/* Trace logging                      */
/* These are written quickly to memory*/
/* buffer for tracing register writes */
/* and reads.                         */
/**************************************/
#define TRACE_ID_RESET_START     0x01
#define TRACE_ID_RESET_DONE      0x02
#define TRACE_ID_FIFO_OVERRUN    0x03
#define TRACE_ID_ASSERT_IRQ      0x04
#define TRACE_ID_REGWR           0x10
#define TRACE_ID_REGRD           0x11
#define TRACE_ID_COMMAND         0x12
#define TRACE_ID_SENDDATA_START  0x20
#define TRACE_ID_SENDDATA_DONE   0x21
#define TRACE_ID_SENDDATA_ABORT  0x22
#define TRACE_ID_RECVDATA_START  0x24
#define TRACE_ID_RECVDATA_DONE   0x25
#define TRACE_ID_RECVDATA_ABORT  0x26
#define TRACE_ID_MUXCR_LOAD      0x30
#define TRACE_REGWR(addr, data) ((uint32_t)TRACE_ID_REGWR | (((addr) & 0xFF) << 8) | (((data) & 0xFFFF) << 16))
#define TRACE_REGRD(addr, data) ((uint32_t)TRACE_ID_REGRD | (((addr) & 0xFF) << 8) | (((data) & 0xFFFF) << 16))
#define TRACE_COMMAND(cmd) ((uint32_t)TRACE_ID_COMMAND | (((cmd) & 0xFF) << 8))
#define TRACE_LOGSIZE 256

static struct
{
    volatile uint32_t wrpos;
    volatile uint32_t rdpos;
    volatile uint32_t buffer[TRACE_LOGSIZE];
} g_ide_phy_trace;

static inline void trace_write(uint32_t msg)
{
    uint32_t pos = g_ide_phy_trace.wrpos;
    g_ide_phy_trace.buffer[pos & (TRACE_LOGSIZE - 1)] = msg;
    g_ide_phy_trace.wrpos = pos + 1;
}

static inline uint32_t trace_read()
{
    uint32_t rdpos = g_ide_phy_trace.rdpos;
    uint32_t wrpos = g_ide_phy_trace.wrpos;
    if ((uint32_t)(wrpos - rdpos) == 0)
    {
        // FIFO empty
        return 0;
    }
    else if ((uint32_t)(wrpos - rdpos) >= TRACE_LOGSIZE)
    {
        // FIFO overrun
        rdpos = wrpos - TRACE_LOGSIZE + 1;
    }

    g_ide_phy_trace.rdpos = rdpos + 1;
    return g_ide_phy_trace.buffer[rdpos & (TRACE_LOGSIZE - 1)];
}

/********************************************/
/* Ring buffer for ide_phy_msg_t structures */
/********************************************/

// RP2040 inter-core FIFO has 8 entries.
// Allocate the message FIFO a bit larger to make sure pointers stay valid.
#define MSG_FIFO_SIZE 16
#define MSG_FIFO_MASK 15

__attribute__((section(".scratch_y.idephy_msgs")))
static struct {
    uint32_t sndidx;
    uint32_t rcvidx;
    ide_phy_msg_t sndbuf[MSG_FIFO_SIZE];
    ide_phy_msg_t rcvbuf[MSG_FIFO_SIZE];
} g_ide_phy_msgs;

static inline ide_phy_msg_t *ide_phy_get_sndbuf()
{
    uint32_t idx = g_ide_phy_msgs.sndidx;
    g_ide_phy_msgs.sndidx = idx + 1;
    return &g_ide_phy_msgs.sndbuf[idx & MSG_FIFO_MASK];
}

static inline ide_phy_msg_t *ide_phy_get_rcvbuf()
{
    uint32_t idx = g_ide_phy_msgs.rcvidx;
    g_ide_phy_msgs.rcvidx = idx + 1;
    return &g_ide_phy_msgs.rcvbuf[idx & MSG_FIFO_MASK];
}

/********************************************/
/* PHY initialization code                  */
/********************************************/

static inline void ide_set_status(uint8_t status)
{
    g_ide_registers[REGRD_ALT_STATUS] = status;
    g_ide_registers[REGRD_STATUS] = status;
}

// Load value to register X of a PIO state machine
static inline void idepio_load_regx(uint sm, uint32_t value)
{
    pio_sm_put(IDE_PIO, sm, value);
    pio_sm_exec(IDE_PIO, sm, pio_encode_pull(false, true));
    pio_sm_exec(IDE_PIO, sm, pio_encode_mov(pio_x, pio_osr));
}

// Load value to register Y of a PIO state machine
static inline void idepio_load_regy(uint sm, uint32_t value)
{
    pio_sm_put(IDE_PIO, sm, value);
    pio_sm_exec(IDE_PIO, sm, pio_encode_pull(false, true));
    pio_sm_exec(IDE_PIO, sm, pio_encode_mov(pio_y, pio_osr));
}

static void ide_phy_init()
{
    if (!g_ide_phy.channels_claimed)
    {
        pio_sm_claim(IDE_PIO, IDE_PIO_SM_CRLOAD);
        pio_sm_claim(IDE_PIO, IDE_PIO_SM_REGWR);
        pio_sm_claim(IDE_PIO, IDE_PIO_SM_REGRD);
        dma_channel_claim(IDE_DMACH_REGWR1);
        dma_channel_claim(IDE_DMACH_REGWR2);
        dma_channel_claim(IDE_DMACH_REGWR3);
        dma_channel_claim(IDE_DMACH_REGRD1);
        dma_channel_claim(IDE_DMACH_REGRD2);
        g_ide_phy.channels_claimed = true;
    }

    // Fill in register write lookup table.
    // Some of the write addresses overlap the read address for a different register,
    // so they have to go through an extra lookup table.
    for (int i = 0; i < 32; i++) { g_ide_register_wr_lookup[i] = &g_ide_phy.invalid_addr; }
    g_ide_register_wr_lookup[REGWR_DEVICE_CONTROL] = &g_ide_phy.device_control;
    g_ide_register_wr_lookup[REGWR_FEATURE    ]    = &g_ide_phy.feature;
    g_ide_register_wr_lookup[REGWR_COMMAND    ]    = &g_ide_phy.command;
    g_ide_register_wr_lookup[REGWR_SECTORCOUNT]    = &g_ide_registers[REGRD_SECTORCOUNT];
    g_ide_register_wr_lookup[REGWR_LBALOW     ]    = &g_ide_registers[REGRD_LBALOW     ];
    g_ide_register_wr_lookup[REGWR_LBAMID     ]    = &g_ide_registers[REGRD_LBAMID     ];
    g_ide_register_wr_lookup[REGWR_LBAHIGH    ]    = &g_ide_registers[REGRD_LBAHIGH    ];
    g_ide_register_wr_lookup[REGWR_DEVICE     ]    = &g_ide_registers[REGRD_DEVICE     ];
    g_ide_register_wr_lookup[REGWR_DATA       ]    = &g_ide_registers[REGRD_DATA       ];

    // Load PIO programs
    pio_clear_instruction_memory(IDE_PIO);
    g_ide_phy.pio_offset_ide_cr_loader = pio_add_program(IDE_PIO, &ide_cr_loader_program);
    g_ide_phy.pio_cfg_ide_cr_loader = ide_cr_loader_program_get_default_config(g_ide_phy.pio_offset_ide_cr_loader);
    sm_config_set_out_shift(&g_ide_phy.pio_cfg_ide_reg_write, true, false, 32);
    sm_config_set_out_pins(&g_ide_phy.pio_cfg_ide_cr_loader, IDE_IO_SHIFT, 16);
    sm_config_set_sideset_pins(&g_ide_phy.pio_cfg_ide_cr_loader, MUX_SEL);

    g_ide_phy.pio_offset_ide_reg_write = pio_add_program(IDE_PIO, &ide_reg_write_program);
    g_ide_phy.pio_cfg_ide_reg_write = ide_reg_write_program_get_default_config(g_ide_phy.pio_offset_ide_reg_write);
    sm_config_set_out_shift(&g_ide_phy.pio_cfg_ide_reg_write, true, false, 32);
    sm_config_set_in_shift(&g_ide_phy.pio_cfg_ide_reg_write, true, true, 30);
    sm_config_set_in_pins(&g_ide_phy.pio_cfg_ide_reg_write, IDE_IO_SHIFT);
    sm_config_set_out_pins(&g_ide_phy.pio_cfg_ide_reg_write, IDE_IO_SHIFT, 16);
    sm_config_set_sideset_pins(&g_ide_phy.pio_cfg_ide_reg_write, CR_DATA_SEL);

    g_ide_phy.pio_offset_ide_reg_read = pio_add_program(IDE_PIO, &ide_reg_read_program);
    g_ide_phy.pio_cfg_ide_reg_read = ide_reg_read_program_get_default_config(g_ide_phy.pio_offset_ide_reg_read);
    sm_config_set_out_shift(&g_ide_phy.pio_cfg_ide_reg_read, true, false, 32);
    sm_config_set_in_shift(&g_ide_phy.pio_cfg_ide_reg_read, true, true, 31);
    sm_config_set_in_pins(&g_ide_phy.pio_cfg_ide_reg_read, IDE_IO_SHIFT);
    sm_config_set_out_pins(&g_ide_phy.pio_cfg_ide_reg_read, IDE_IO_SHIFT, 16);
    sm_config_set_sideset_pins(&g_ide_phy.pio_cfg_ide_reg_read, CR_DATA_SEL);

    // Initialize state machines
    pio_sm_init(IDE_PIO, IDE_PIO_SM_CRLOAD, g_ide_phy.pio_offset_ide_cr_loader, &g_ide_phy.pio_cfg_ide_cr_loader);
    pio_sm_init(IDE_PIO, IDE_PIO_SM_REGWR, g_ide_phy.pio_offset_ide_reg_write, &g_ide_phy.pio_cfg_ide_reg_write);
    pio_sm_init(IDE_PIO, IDE_PIO_SM_REGRD, g_ide_phy.pio_offset_ide_reg_read, &g_ide_phy.pio_cfg_ide_reg_read);

    // Load base addresses for register access
    idepio_load_regy(IDE_PIO_SM_REGWR, ((uint32_t)g_ide_register_wr_lookup) >> 7);
    idepio_load_regy(IDE_PIO_SM_REGRD, ((uint32_t)g_ide_registers) >> 6);

    // Configure register write DMA chain
    // 1. IDE_PIO_SM_REGWR generates lookup address to g_ide_register_wr_lookup
    // 2. IDE_DMACH_REGWR1 copies lookup address to IDE_DMACH_REGWR2 source address
    // 3. IDE_DMACH_REGWR2 copies value from lookup table to IDE_DMACH_REGWR3 destination address
    // 4. IDE_DMACH_REGWR3 copies data from IDE_PIO_SM_REGWR to memory
    dma_channel_config cfg;
    cfg = dma_channel_get_default_config(IDE_DMACH_REGWR1);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&cfg, false);
    channel_config_set_write_increment(&cfg, false);
    channel_config_set_dreq(&cfg, pio_get_dreq(IDE_PIO, IDE_PIO_SM_REGWR, false));
    dma_channel_configure(IDE_DMACH_REGWR1, &cfg,
        &dma_hw->ch[IDE_DMACH_REGWR2].al3_read_addr_trig,
        &IDE_PIO->rxf[IDE_PIO_SM_REGWR], 1, false);

    cfg = dma_channel_get_default_config(IDE_DMACH_REGWR2);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&cfg, false);
    channel_config_set_write_increment(&cfg, false);
    dma_channel_configure(IDE_DMACH_REGWR2, &cfg,
        &dma_hw->ch[IDE_DMACH_REGWR3].al2_write_addr_trig,
        NULL, 1, false);

    cfg = dma_channel_get_default_config(IDE_DMACH_REGWR3);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_16);
    channel_config_set_read_increment(&cfg, false);
    channel_config_set_write_increment(&cfg, false);
    channel_config_set_chain_to(&cfg, IDE_DMACH_REGWR1);
    channel_config_set_dreq(&cfg, pio_get_dreq(IDE_PIO, IDE_PIO_SM_REGWR, false));
    dma_channel_configure(IDE_DMACH_REGWR3, &cfg,
        NULL,
        &IDE_PIO->rxf[IDE_PIO_SM_REGWR], 1, false);

    // Configure register read DMA chain
    // 1. IDE_PIO_SM_REGRD generates address to g_ide_registers
    // 2. IDE_DMACH_REGRD1 copies address to IDE_DMACH_REGRD2 source address
    // 3. IDE_DMACH_REGRD2 copies data to IDE_PIO_SM_REGRD
    cfg = dma_channel_get_default_config(IDE_DMACH_REGRD1);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&cfg, false);
    channel_config_set_write_increment(&cfg, false);
    channel_config_set_dreq(&cfg, pio_get_dreq(IDE_PIO, IDE_PIO_SM_REGRD, false));
    dma_channel_configure(IDE_DMACH_REGRD1, &cfg,
        &dma_hw->ch[IDE_DMACH_REGRD2].al3_read_addr_trig,
        &IDE_PIO->rxf[IDE_PIO_SM_REGRD], 1, false);

    cfg = dma_channel_get_default_config(IDE_DMACH_REGRD2);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_16);
    channel_config_set_read_increment(&cfg, false);
    channel_config_set_write_increment(&cfg, false);
    channel_config_set_chain_to(&cfg, IDE_DMACH_REGRD1);
    dma_channel_configure(IDE_DMACH_REGRD2, &cfg,
        &IDE_PIO->txf[IDE_PIO_SM_REGRD],
        NULL, 1, false);

    // Set initial pin states and directions
    pio_sm_set_pins(IDE_PIO, IDE_PIO_SM_REGWR, BM(IDE_OUT_IORDY));
    pio_sm_set_consecutive_pindirs(IDE_PIO, IDE_PIO_SM_REGWR, 0, 16, false);
    pio_sm_set_consecutive_pindirs(IDE_PIO, IDE_PIO_SM_REGWR, MUX_SEL, 2, true);

    // Assign pins to PIO
    for (int i = 0; i < 16; i++)
    {
        iobank0_hw->io[IDE_IO_SHIFT + i].ctrl  = GPIO_FUNC_PIO0;
    }
    iobank0_hw->io[MUX_SEL].ctrl  = GPIO_FUNC_PIO0;
    iobank0_hw->io[IDE_OUT_IORDY].ctrl  = GPIO_FUNC_PIO0;
}

/********************************************/
/* PHY low-level implementation code        */
/********************************************/

// Check if an emulated device is currently selected
static inline bool ide_check_device_selected()
{
    bool device1 = (g_ide_registers[REGWR_DEVICE] & 0x10);
    return (device1 ? g_ide_phy.device1 : g_ide_phy.device0);
}

// Load new value to the state machine that loads the control register
// If update_now is true, triggers the state machine immediately.
// Otherwise it will be done on next register read/write or by ide_update_control_reg_apply().
static inline void ide_update_control_reg(bool update_now)
{
    uint32_t cr_val = MUXCR_STATUS;

    if (ide_check_device_selected())
    {
        cr_val &= ~BM(CR_NEG_CTRL_OUT);

        if ((g_ide_registers[REGWR_DEVICE_CONTROL] & 0x02) == 0)
        {
            // Interrupt out enabled
            cr_val &= ~BM(CR_NEG_IDE_INTRQ_EN);
        }
    }

    if (g_ide_phy.status_led)
    {
        cr_val |= BM(CR_STATUS_LED);
    }

    cr_val |= 0xFFFF0000; // For setting pin directions
    g_ide_phy.control_reg = cr_val;
    IDE_PIO->txf[IDE_PIO_SM_CRLOAD] = cr_val;
    IDE_PIO->sm[IDE_PIO_SM_CRLOAD].instr = pio_encode_pull(false, false);
    IDE_PIO->sm[IDE_PIO_SM_CRLOAD].instr = pio_encode_mov(pio_x, pio_osr);

    if (update_now)
    {
        IDE_PIO->irq_force = 1; // Start loading
    }
}

// Enable interrupt signal temporarily until next register access
// For strict spec conformance it should stay active until next STATUS register read,
// but in practice the next register access typically is a status read.
static inline void ide_update_control_reg_temporary_irq()
{
    // Interrupt request active
    // NOTE: BM(9) for errata in first PCB prototype version
    IDE_PIO->txf[IDE_PIO_SM_CRLOAD] = g_ide_phy.control_reg | BM(9) | BM(CR_IDE_INTRQ);
    IDE_PIO->sm[IDE_PIO_SM_CRLOAD].instr = pio_encode_pull(false, false);
}

// Apply previously started control registery update
static inline void ide_update_control_reg_apply()
{
    IDE_PIO->irq_force = 1;
}

__attribute__((section(".scratch_y.idephy")))
static void ide_phy_delay_us_core1(uint32_t us)
{
    uint32_t start = timer_hw->timerawl;
    while ((uint32_t)(timer_hw->timerawl - start) < us);
}

// Handle IDE bus reset state
__attribute__((section(".scratch_y.idephy")))
static void ide_phy_reset_loop_core1()
{
    // Stop state machines
    pio_sm_set_enabled(IDE_PIO, IDE_PIO_SM_REGWR, false);
    pio_sm_set_enabled(IDE_PIO, IDE_PIO_SM_REGRD, false);
    pio_sm_set_enabled(IDE_PIO, IDE_PIO_SM_CRLOAD, true);

    g_ide_registers[REGWR_DEVICE_CONTROL] = 0x02;

    trace_write(TRACE_ID_RESET_START);

    // Handle bus reset condition
    do {
        gpio_set_pulls(IDE_IN_RST, true, false); // Pull IDERST high to temporarily enable MUX_OE
        ide_phy_delay_us_core1(2);
        ide_update_control_reg(true);
        ide_phy_delay_us_core1(2);
        gpio_set_pulls(IDE_IN_RST, false, true); // Pull IDERST low to see if mux was autodisabled again
        ide_phy_delay_us_core1(2);
    } while (!(sio_hw->gpio_in & BM(IDE_IN_RST)));

    trace_write(TRACE_ID_RESET_DONE);

    g_ide_registers[REGRD_ERROR] = 0;
    g_ide_registers[REGRD_DEVICE] = 0;
    ide_set_status(IDE_STATUS_DEVRDY);

    // Start state machines
    pio_sm_clear_fifos(IDE_PIO, IDE_PIO_SM_REGWR);
    pio_sm_clear_fifos(IDE_PIO, IDE_PIO_SM_REGRD);
    pio_sm_set_enabled(IDE_PIO, IDE_PIO_SM_REGWR, true);
    pio_sm_set_enabled(IDE_PIO, IDE_PIO_SM_REGRD, true);

    // Start DMA transfers
    const uint32_t channels = BM(IDE_DMACH_REGWR1) | BM(IDE_DMACH_REGWR2) | BM(IDE_DMACH_REGWR3) |
                              BM(IDE_DMACH_REGRD1) | BM(IDE_DMACH_REGRD2);
    *(volatile uint32_t*)&(dma_hw->intr) = channels; // Clear flags
    dma_start_channel_mask(BM(IDE_DMACH_REGWR1) | BM(IDE_DMACH_REGRD1));
}

// Check if any register reads or writes have occurred
__attribute__((section(".scratch_y.idephy")))
static void ide_phy_check_register_event_core1()
{
    uint32_t irq = dma_hw->intr;
    if (irq & BM(IDE_DMACH_REGRD2))
    {
        *(volatile uint32_t*)&(dma_hw->intr) = BM(IDE_DMACH_REGRD2);
        uint32_t lookup_addr = dma_hw->ch[IDE_DMACH_REGRD2].al1_read_addr;
        uint8_t regaddr = (lookup_addr >> 1) & REGADDR_MASK;
        uint8_t data = g_ide_registers[regaddr];
        trace_write(TRACE_REGRD(regaddr, data));
    }

    if (irq & BM(IDE_DMACH_REGWR3))
    {
        *(volatile uint32_t*)&(dma_hw->intr) = BM(IDE_DMACH_REGWR3);
        uint32_t lookup_addr = dma_hw->ch[IDE_DMACH_REGWR2].al1_read_addr;
        uint8_t regaddr = ((lookup_addr) >> 2) & REGADDR_MASK;
        uint8_t data = *g_ide_register_wr_lookup[regaddr];

        if (regaddr == REGWR_COMMAND)
        {
            // New command started
            ide_set_status(IDE_STATUS_BSY | IDE_STATUS_DEVRDY);
            trace_write(TRACE_COMMAND(data));

            if (multicore_fifo_wready())
            {
                ide_phy_msg_t *buf = ide_phy_get_rcvbuf();
                buf->type = IDE_MSG_CMD_START;
                buf->payload.cmd_start.command = data;
                buf->payload.cmd_start.device = g_ide_registers[REGRD_DEVICE];
                buf->payload.cmd_start.features = g_ide_phy.feature;
                buf->payload.cmd_start.sector_count = g_ide_registers[REGRD_SECTORCOUNT];
                buf->payload.cmd_start.lbalow = g_ide_registers[REGRD_LBALOW];
                buf->payload.cmd_start.lbamid = g_ide_registers[REGRD_LBAMID];
                buf->payload.cmd_start.lbahigh = g_ide_registers[REGRD_LBAHIGH];
                __sync_synchronize();
                sio_hw->fifo_wr = (uint32_t)buf;
            }
            else
            {
                trace_write(TRACE_ID_FIFO_OVERRUN);
            }
        }
        else
        {
            // A regular register write
            trace_write(TRACE_REGWR(regaddr, data));

            if (regaddr == REGWR_DEVICE_CONTROL)
            {
                ide_update_control_reg(false);

                if (data & 0x04)
                {
                    // Report software reset to protocol level
                    if (multicore_fifo_wready())
                    {
                        ide_phy_msg_t *buf = ide_phy_get_rcvbuf();
                        buf->type = IDE_MSG_RESET;
                        buf->payload.reset.device_control = g_ide_phy.device_control;
                        __sync_synchronize();
                        sio_hw->fifo_wr = (uint32_t)buf;
                    }
                }
            }
            else if (regaddr == REGWR_DEVICE)
            {
                if (ide_check_device_selected())
                {
                    pio_sm_set_enabled(IDE_PIO, IDE_PIO_SM_REGRD, true);
                    ide_update_control_reg(false);
                }
                else
                {
                    pio_sm_set_enabled(IDE_PIO, IDE_PIO_SM_REGRD, false);
                    ide_update_control_reg(true);
                }
            }
        }
    }
}

// Send data to host
__attribute__((section(".scratch_y.idephy")))
static bool ide_phy_send_data_core1(uint32_t words, const uint16_t *data, bool assert_irq)
{
    trace_write(TRACE_ID_SENDDATA_START | (words << 8));

    // Load bytes to simulated data register each time host reads it.
    g_ide_registers[REGRD_DATA] = data[0];
    
    if (assert_irq)
    {
        // This sequence ensures that we are able to start sending immediately
        // after the interrupt assert takes effect.
        ide_update_control_reg_temporary_irq();
        ide_set_status(IDE_STATUS_DEVRDY | IDE_STATUS_DATAREQ);
        ide_update_control_reg_apply();
    }
    else
    {
        ide_set_status(IDE_STATUS_DEVRDY | IDE_STATUS_DATAREQ);
    }

    uint32_t done = 1;
    while (done < words)
    {
        uint32_t irq;
        
        while (((irq = dma_hw->intr) & BM(IDE_DMACH_REGRD2)) && done < words)
        {
            // Process reads in tight loop
            *(volatile uint32_t*)&(dma_hw->intr) = BM(IDE_DMACH_REGRD2);
            if (dma_hw->ch[IDE_DMACH_REGRD2].al1_read_addr == (uint32_t)&g_ide_registers[REGRD_DATA])
            {
                g_ide_registers[REGRD_DATA] = data[done++];
            }
        }
        
        if (irq & BM(IDE_DMACH_REGWR3))
        {
            // Host wrote register, abort data transfer
            trace_write(TRACE_ID_SENDDATA_ABORT | (done << 8));
            ide_set_status(IDE_STATUS_DEVRDY | IDE_STATUS_BSY);
            return false;
        }
    }

    // Wait for last byte
    while (!(dma_hw->intr & BM(IDE_DMACH_REGRD2)));
    *(volatile uint32_t*)&(dma_hw->intr) = BM(IDE_DMACH_REGRD2);

    g_ide_registers[REGRD_DATA] = 0;
    trace_write(TRACE_ID_SENDDATA_DONE);
    ide_set_status(IDE_STATUS_DEVRDY | IDE_STATUS_BSY);
    return true;
}

// Receive data from host
__attribute__((section(".scratch_y.idephy")))
static bool ide_phy_recv_data_core1(uint32_t words, uint16_t *data, bool assert_irq)
{
    trace_write(TRACE_ID_RECVDATA_START | (words << 8));

    if (assert_irq)
    {
        // This sequence ensures that we are able to start receiving immediately
        // after the interrupt assert takes effect.
        ide_update_control_reg_temporary_irq();
        ide_set_status(IDE_STATUS_DEVRDY | IDE_STATUS_DATAREQ);
        ide_update_control_reg_apply();
    }
    else
    {
        ide_set_status(IDE_STATUS_DEVRDY | IDE_STATUS_DATAREQ);
    }

    uint32_t done = 0;
    while (done < words)
    {
        uint32_t irq = dma_hw->intr;

        if (irq & BM(IDE_DMACH_REGWR3))
        {
            uint32_t lookup_addr = dma_hw->ch[IDE_DMACH_REGWR2].al1_read_addr;
            if (lookup_addr == (uint32_t)&g_ide_register_wr_lookup[REGWR_DATA])
            {
                // Advance write pointer for data register
                data[done++] = g_ide_registers[REGRD_DATA];
                *(volatile uint32_t*)&(dma_hw->intr) = BM(IDE_DMACH_REGWR3);
            }
            else
            {
                // Host wrote other register, abort data transfer
                trace_write(TRACE_ID_RECVDATA_ABORT | (done << 8));
                ide_set_status(IDE_STATUS_DEVRDY | IDE_STATUS_BSY);
                return false;
            }
        }
    }

    trace_write(TRACE_ID_RECVDATA_DONE);
    ide_set_status(IDE_STATUS_DEVRDY | IDE_STATUS_BSY);
    return true;
}

__attribute__((section(".scratch_y.idephy")))
static void ide_phy_loop_core1()
{
    multicore_fifo_drain();

    bool first = true;

    while (1)
    {
        // Check for reset conditions
        if (first || !(sio_hw->gpio_in & BM(IDE_IN_RST)))
        {
            first = false;
            ide_phy_reset_loop_core1();

            // Report hardware reset to protocol level
            if (multicore_fifo_wready())
            {
                ide_phy_msg_t *buf = ide_phy_get_rcvbuf();
                buf->type = IDE_MSG_RESET;
                buf->payload.reset.device_control = g_ide_phy.device_control;
                __sync_synchronize();
                sio_hw->fifo_wr = (uint32_t)buf;
            }
        }

        // Check hardware register read/writes handled by PIO & DMA
        ide_phy_check_register_event_core1();
        
        // Process commands sent from core 0
        if (multicore_fifo_rvalid())
        {
            ide_phy_msg_t *buf = (ide_phy_msg_t*)sio_hw->fifo_rd;
            *buf->status = IDE_MSGSTAT_EXECUTING;
            if (buf->type == IDE_MSG_DEVICE_RDY)
            {
                g_ide_registers[REGRD_ERROR] = buf->payload.device_rdy.error;

                if (buf->payload.device_rdy.set_registers)
                {
                    g_ide_registers[REGRD_SECTORCOUNT] = buf->payload.device_rdy.sector_count;
                    g_ide_registers[REGRD_LBALOW] = buf->payload.device_rdy.lbalow;
                    g_ide_registers[REGRD_LBAMID] = buf->payload.device_rdy.lbamid;
                    g_ide_registers[REGRD_LBAHIGH] = buf->payload.device_rdy.lbahigh;

                    uint8_t devreg = g_ide_registers[REGRD_DEVICE];
                    devreg &= 0x10; // Keep drive bit
                    devreg |= (buf->payload.device_rdy.device) & (~0x10);
                    g_ide_registers[REGRD_DEVICE] = devreg;
                }

                if (buf->payload.device_rdy.status != 0)
                {
                    ide_set_status(buf->payload.device_rdy.status);
                }
                else if (buf->payload.device_rdy.error != 0)
                {
                    ide_set_status(IDE_STATUS_DEVRDY | IDE_STATUS_ERR);
                }
                else
                {
                    ide_set_status(IDE_STATUS_DEVRDY);
                }

                if (buf->payload.device_rdy.assert_irq)
                {
                    ide_update_control_reg_temporary_irq();
                    ide_update_control_reg_apply();
                }
                *buf->status = IDE_MSGSTAT_SUCCESS;
            }
            else if (buf->type == IDE_MSG_SEND_DATA)
            {
                if (ide_phy_send_data_core1(buf->payload.send_data.words,
                        buf->payload.send_data.data, buf->payload.send_data.assert_irq))
                {
                    *buf->status = IDE_MSGSTAT_SUCCESS;
                }
                else
                {
                    *buf->status = IDE_MSGSTAT_ABORTED;
                }
            }
            else if (buf->type == IDE_MSG_RECV_DATA)
            {
                if (ide_phy_recv_data_core1(buf->payload.recv_data.words,
                        buf->payload.recv_data.data, buf->payload.recv_data.assert_irq))
                {
                    *buf->status = IDE_MSGSTAT_SUCCESS;
                }
                else
                {
                    *buf->status = IDE_MSGSTAT_ABORTED;
                }
            }
            else if (buf->type == IDE_MSG_PLATFORM_0)
            {
                // Platform message is to update control register for LED blinking
                g_ide_phy.status_led = buf->payload.raw[0];
                ide_update_control_reg(true);
                *buf->status = IDE_MSGSTAT_SUCCESS;
            }
            else
            {
                *buf->status = IDE_MSGSTAT_ERROR;
            }

            buf->status = nullptr;
        }
    }
}

/**************************************************/
/* API implementation for application access      */
/* Function definitions are in ide_phy.h          */
/**************************************************/

// When low-level debug is requested, attempt to log register accesses.
// Because of speed constraints, sometimes a few accesses may be lost.
static void print_trace_log()
{
    uint32_t prev_msg = 0;
    uint32_t msg = 0;
    int repeat_count = 0;
    while (1)
    {
        msg = trace_read();
        if (msg == prev_msg)
        {
            repeat_count++;
            continue;
        }
        else
        {
            if (repeat_count > 0)
            {
                azdbg("---- repeat x ", (repeat_count + 1));
            }
            repeat_count = 0;
        }
        prev_msg = msg;

        if (!msg)
        {
            break;
        }

        uint8_t id = msg & 0xFF;
        if (id == TRACE_ID_RESET_START)
        {
            azdbg("-- IDE reset start");
        }
        else if (id == TRACE_ID_RESET_DONE)
        {
            azdbg("-- IDE reset complete");
        }
        else if (id == TRACE_ID_REGWR)
        {
            uint8_t regaddr = (uint8_t)(msg >> 8);
            uint16_t value = (uint16_t)(msg >> 16);
            switch (regaddr & REGADDR_MASK)
            {
                case REGWR_DEVICE_CONTROL: azdbg("-- WR DEVICE_CONTROL <=  ", (uint8_t)value); break;
                case REGWR_FEATURE:        azdbg("-- WR FEATURE        <=  ", (uint8_t)value); break;
                case REGWR_SECTORCOUNT:    azdbg("-- WR SECTORCOUNT    <=  ", (uint8_t)value); break;
                case REGWR_LBALOW:         azdbg("-- WR LBALOW         <=  ", (uint8_t)value); break;
                case REGWR_LBAMID:         azdbg("-- WR LBAMID         <=  ", (uint8_t)value); break;
                case REGWR_LBAHIGH:        azdbg("-- WR LBAHIGH        <=  ", (uint8_t)value); break;
                case REGWR_DEVICE:         azdbg("-- WR DEVICE         <=  ", (uint8_t)value); break;
                case REGWR_COMMAND:        azdbg("-- WR COMMAND        <=  ", (uint8_t)value); break;
                case REGWR_DATA:           azdbg("-- WR DATA           <=  ", value); break;
                default:                   azdbg("-- WR UNKNOWN (", regaddr, ") <= ", value); break;
            }
        }
        else if (id == TRACE_ID_REGRD)
        {
            uint8_t regaddr = (uint8_t)(msg >> 8);
            uint16_t value = (uint16_t)(msg >> 16);
            switch (regaddr & REGADDR_MASK)
            {
                case REGRD_ALT_STATUS:     azdbg("-- RD ALT_STATUS      => ", (uint8_t)value); break;
                case REGRD_ERROR:          azdbg("-- RD ERROR           => ", (uint8_t)value); break;
                case REGRD_SECTORCOUNT:    azdbg("-- RD SECTORCOUNT     => ", (uint8_t)value); break;
                case REGRD_LBALOW:         azdbg("-- RD LBALOW          => ", (uint8_t)value); break;
                case REGRD_LBAMID:         azdbg("-- RD LBAMID          => ", (uint8_t)value); break;
                case REGRD_LBAHIGH:        azdbg("-- RD LBAHIGH         => ", (uint8_t)value); break;
                case REGRD_DEVICE:         azdbg("-- RD DEVICE          => ", (uint8_t)value); break;
                case REGRD_STATUS:         azdbg("-- RD STATUS          => ", (uint8_t)value); break;
                case REGRD_DATA:           azdbg("-- RD DATA            => ", value); break;
                default:                   azdbg("-- RD UNKNOWN (", regaddr, ") => ", value); break;
            }
        }
        else if (id == TRACE_ID_COMMAND)
        {
            uint8_t command = (uint8_t)(msg >> 8);
            azdbg("-- CMD ", command);
        }
        else if (id == TRACE_ID_ASSERT_IRQ)
        {
            azdbg("-- ASSERT IRQ");
        }
        else if (id == TRACE_ID_FIFO_OVERRUN)
        {
            azdbg("-- PHY RX FIFO OVERRUN");
        }
        else if (id == TRACE_ID_SENDDATA_START)
        {
            azdbg("-- SENDDATA ", (int)(msg >> 8), " words");
        }
        else if (id == TRACE_ID_SENDDATA_DONE)
        {
            azdbg("-- SENDDATA DONE");
        }
        else if (id == TRACE_ID_SENDDATA_ABORT)
        {
            azdbg("-- SENDDATA ABORT after", (int)(msg >> 8), " words");
        }
        else if (id == TRACE_ID_RECVDATA_START)
        {
            azdbg("-- RECVDATA ", (int)(msg >> 8), " words");
        }
        else if (id == TRACE_ID_RECVDATA_DONE)
        {
            azdbg("-- RECVDATA DONE");
        }
        else if (id == TRACE_ID_RECVDATA_ABORT)
        {
            azdbg("-- RECVDATA ABORT after", (int)(msg >> 8), " words");
        }
        else if (id == TRACE_ID_MUXCR_LOAD)
        {
            azdbg("-- MUXCR LOAD ", (uint32_t)(msg >> 8));
        }
    }
}

static void ide_phy_stop()
{
    // Stop second core
    multicore_reset_core1();
    multicore_fifo_drain();

    // Abort all ongoing requests
    for (int i = 0; i < MSG_FIFO_SIZE; i++)
    {
        if (g_ide_phy_msgs.sndbuf[i].status)
        {
            *g_ide_phy_msgs.sndbuf[i].status = IDE_MSGSTAT_ABORTED;
            g_ide_phy_msgs.sndbuf[i].status = nullptr;
        }
    }
}

static void ide_phy_start()
{
    ide_phy_init();
    multicore_launch_core1_with_stack(&ide_phy_loop_core1, g_ide_phy_core1_stack, sizeof(g_ide_phy_core1_stack));
}

void ide_phy_reset_from_watchdog()
{
    ide_phy_stop();
    ide_phy_start();
}

void ide_phy_reset(bool has_dev0, bool has_dev1)
{
    ide_phy_stop();

    // Reinitialize configuration
    g_ide_phy.device0 = has_dev0;
    g_ide_phy.device1 = has_dev1;
    g_ide_phy.lowlevel_trace = ini_getbool("IDE", "Trace", g_ide_phy.lowlevel_trace, CONFIGFILE);

    ide_phy_start();
}

ide_phy_msg_t *ide_phy_get_msg()
{
    if (g_azlog_debug && g_ide_phy.lowlevel_trace)
    {
        print_trace_log();
    }

    if (multicore_fifo_rvalid())
    {
        return (ide_phy_msg_t *)sio_hw->fifo_rd;
    }
    else
    {
        return NULL;
    }
}

bool ide_phy_send_msg(ide_phy_msg_t *msg)
{
    static ide_msg_status_t dummy_status;
    bool retval = false;

    if (multicore_fifo_wready())
    {
        ide_phy_msg_t *buf = ide_phy_get_sndbuf();
        memcpy(buf, msg, sizeof(ide_phy_msg_t));
        if (buf->status == NULL) buf->status = &dummy_status;
        __sync_synchronize();
        sio_hw->fifo_wr = (uint32_t)buf;
        
        *buf->status = IDE_MSGSTAT_QUEUED;
        retval = true;
    }
    
    if (g_azlog_debug && g_ide_phy.lowlevel_trace)
    {
        print_trace_log();
    }

    return retval;
}

