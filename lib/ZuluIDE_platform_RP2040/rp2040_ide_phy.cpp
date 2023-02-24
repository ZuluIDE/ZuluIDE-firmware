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

// Mux control values when:
// 1) monitoring status signals
// 2) register write
// 3) register read
#define MUXCR_STATUS \
     (BM(CR_MUX_OE) | \
      BM(CR_NEG_CTRL_OUT) | \
      BM(CR_NEG_IDE_IOCS16) | \
      BM(CR_NEG_IDE_PDIAG) | \
      BM(CR_NEG_IDE_DASP) | \
      BM(CR_NEG_IDE_INTRQ_EN))

#define MUXCR_WRITE \
     (BM(CR_MUX_OE) | \
      BM(CR_DATA_SEL) | \
      BM(CR_NEG_IDE_IOCS16) | \
      BM(CR_NEG_IDE_PDIAG) | \
      BM(CR_NEG_IDE_DASP) | \
      BM(CR_NEG_IDE_INTRQ_EN))

#define MUXCR_READ \
     (BM(CR_MUX_OE) | \
      BM(CR_DATA_SEL) | \
      BM(CR_DATA_DIR) | \
      BM(CR_NEG_IDE_IOCS16) | \
      BM(CR_NEG_IDE_PDIAG) | \
      BM(CR_NEG_IDE_DASP) | \
      BM(CR_NEG_IDE_INTRQ_EN))

#define IDE_PIO pio0
#define IDE_PIO_SM_REGWR 0
#define IDE_PIO_SM_REGRD 1

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
__attribute__((section(".scratch_x.idepio"), aligned(32)))
static uint8_t g_ide_registers[32];

// Lookup table to get the address for register writes
// Addressed by REGWR_ addresses
__attribute__((section(".scratch_x.idepio"), aligned(32 * 4)))
static uint8_t *g_ide_register_wr_lookup[32];

__attribute__((section(".scratch_x.idepio")))
static struct {
    // Write-only registers
    uint8_t command;
    uint8_t feature;
    uint8_t device_control;
    uint8_t invalid_addr;

    bool channels_claimed;
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
#define TRACE_ID_RESET_START   0x01
#define TRACE_ID_RESET_DONE    0x02
#define TRACE_ID_FIFO_OVERRUN  0x03
#define TRACE_ID_REGWR         0x10
#define TRACE_ID_REGRD         0x11
#define TRACE_ID_COMMAND       0x12
#define TRACE_REGWR(addr, data) ((uint32_t)TRACE_ID_REGWR | (((addr) & 0xFF) << 8) | (((data) & 0xFFFF) << 16))
#define TRACE_REGRD(addr, data) ((uint32_t)TRACE_ID_REGRD | (((addr) & 0xFF) << 8) | (((data) & 0xFFFF) << 16))
#define TRACE_COMMAND(cmd) ((uint32_t)TRACE_ID_COMMAND | (((cmd) & 0xFF) << 8))
#define TRACE_LOGSIZE 64

__attribute__((section(".scratch_y.idephy_trace")))
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

    // Load PIO programs
    pio_clear_instruction_memory(IDE_PIO);
    g_ide_phy.pio_offset_ide_reg_write = pio_add_program(IDE_PIO, &ide_reg_write_program);
    g_ide_phy.pio_cfg_ide_reg_write = ide_reg_write_program_get_default_config(g_ide_phy.pio_offset_ide_reg_write);
    sm_config_set_out_shift(&g_ide_phy.pio_cfg_ide_reg_write, true, false, 32);
    sm_config_set_in_shift(&g_ide_phy.pio_cfg_ide_reg_write, false, true, 32);
    sm_config_set_in_pins(&g_ide_phy.pio_cfg_ide_reg_write, IDE_IO_SHIFT);
    sm_config_set_out_pins(&g_ide_phy.pio_cfg_ide_reg_write, IDE_IO_SHIFT, 16);
    sm_config_set_sideset_pins(&g_ide_phy.pio_cfg_ide_reg_write, MUX_SEL);

    g_ide_phy.pio_offset_ide_reg_read = pio_add_program(IDE_PIO, &ide_reg_read_program);
    g_ide_phy.pio_cfg_ide_reg_read = ide_reg_read_program_get_default_config(g_ide_phy.pio_offset_ide_reg_read);
    sm_config_set_out_shift(&g_ide_phy.pio_cfg_ide_reg_read, true, false, 32);
    sm_config_set_in_shift(&g_ide_phy.pio_cfg_ide_reg_read, false, true, 32);
    sm_config_set_in_pins(&g_ide_phy.pio_cfg_ide_reg_read, IDE_IO_SHIFT);
    sm_config_set_out_pins(&g_ide_phy.pio_cfg_ide_reg_read, IDE_IO_SHIFT, 16);
    sm_config_set_sideset_pins(&g_ide_phy.pio_cfg_ide_reg_read, MUX_SEL);

    // Initialize register write watcher
    pio_sm_init(IDE_PIO, IDE_PIO_SM_REGWR, g_ide_phy.pio_offset_ide_reg_write, &g_ide_phy.pio_cfg_ide_reg_write);
    idepio_load_regx(IDE_PIO_SM_REGWR, MUXCR_WRITE | (REVBITS(MUXCR_STATUS) << 16));
    idepio_load_regy(IDE_PIO_SM_REGWR, ((uint32_t)g_ide_register_wr_lookup) >> 7);

    // Initialize register read watcher
    pio_sm_init(IDE_PIO, IDE_PIO_SM_REGRD, g_ide_phy.pio_offset_ide_reg_read, &g_ide_phy.pio_cfg_ide_reg_read);
    idepio_load_regx(IDE_PIO_SM_REGRD, MUXCR_READ | (REVBITS(MUXCR_STATUS) << 16));
    idepio_load_regy(IDE_PIO_SM_REGRD, ((uint32_t)g_ide_registers) >> 5);

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
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_8);
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
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_8);
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

    // Turn off input synchronizers in order to respond fast enough to strobes
    IDE_PIO->input_sync_bypass |= BM(IDE_IN_DIOR) | BM(IDE_IN_DIOW);

    // Assign pins to PIO
    for (int i = 0; i < 16; i++)
    {
        iobank0_hw->io[IDE_IO_SHIFT + i].ctrl  = GPIO_FUNC_PIO0;
    }
    iobank0_hw->io[MUX_SEL].ctrl  = GPIO_FUNC_PIO0;
    iobank0_hw->io[IDE_OUT_IORDY].ctrl  = GPIO_FUNC_PIO0;
}

static void ide_phy_write_mux_ctrl(uint16_t value)
{
    pio_sm_put(IDE_PIO, IDE_PIO_SM_REGWR, 0xFFFF0000 | value);
    pio_sm_exec(IDE_PIO, IDE_PIO_SM_REGWR, pio_encode_pull(false, true) | pio_encode_sideset_opt(1, 0));
    pio_sm_exec(IDE_PIO, IDE_PIO_SM_REGWR, pio_encode_out(pio_pins, 16));
    pio_sm_exec(IDE_PIO, IDE_PIO_SM_REGWR, pio_encode_out(pio_pindirs, 16));
    pio_sm_exec(IDE_PIO, IDE_PIO_SM_REGWR, pio_encode_out(pio_pindirs, 16) | pio_encode_sideset_opt(1, 1));
}

/********************************************/
/* PHY low-level implementation code        */
/********************************************/

__attribute__((section(".scratch_y.idephy")))
static void ide_phy_reset_loop_core1()
{
    // Stop state machines
    pio_sm_set_enabled(IDE_PIO, IDE_PIO_SM_REGWR, false);
    pio_sm_set_enabled(IDE_PIO, IDE_PIO_SM_REGRD, false);

    trace_write(TRACE_ID_RESET_START);

    // Handle bus reset condition
    do {
        gpio_set_pulls(IDE_IN_RST, true, false); // Pull IDERST high to temporarily enable MUX_OE
        ide_phy_write_mux_ctrl(MUXCR_STATUS);
        delay_100ns();
        gpio_set_pulls(IDE_IN_RST, false, true); // Pull IDERST low to see if mux was autodisabled again
        delay_100ns();
    } while (!(sio_hw->gpio_in & BM(IDE_IN_RST)));

    trace_write(TRACE_ID_RESET_DONE);

    g_ide_registers[REGRD_ERROR] = 0;
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

__attribute__((section(".scratch_y.idephy")))
static void ide_phy_loop_core1()
{
    multicore_fifo_drain();
    ide_phy_reset_loop_core1();

    while (1)
    {
        if (!(sio_hw->gpio_in & BM(IDE_IN_RST)))
        {
            ide_phy_reset_loop_core1();
        }

        uint32_t irq = dma_hw->intr;
        if (irq & BM(IDE_DMACH_REGWR3))
        {
            *(volatile uint32_t*)&(dma_hw->intr) = BM(IDE_DMACH_REGWR3);
            uint32_t lookup_addr = dma_hw->ch[IDE_DMACH_REGWR2].al1_read_addr;
            uint8_t regaddr = ((lookup_addr) >> 2) & REGADDR_MASK;
            uint8_t data = **(uint8_t**)lookup_addr;

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
                    buf->payload.cmd_start.lba =
                        ((uint32_t)(g_ide_registers[REGRD_LBALOW]) << 0) |
                        ((uint32_t)(g_ide_registers[REGRD_LBAMID]) << 8) |
                        ((uint32_t)(g_ide_registers[REGRD_LBAHIGH]) << 16) |
                        ((uint32_t)(g_ide_registers[REGRD_DEVICE] & 0x0F) << 24);
                    sio_hw->fifo_wr = (uint32_t)buf;
                }
                else
                {
                    trace_write(TRACE_ID_FIFO_OVERRUN);
                }
            }
            else
            {
                // Just a regular register write
                trace_write(TRACE_REGWR(regaddr, data));
            }
        }

        if (irq & BM(IDE_DMACH_REGRD2))
        {
            *(volatile uint32_t*)&(dma_hw->intr) = BM(IDE_DMACH_REGRD2);
            uint32_t lookup_addr = dma_hw->ch[IDE_DMACH_REGRD2].al1_read_addr;
            uint8_t regaddr = lookup_addr & REGADDR_MASK;
            uint8_t data = *(uint8_t*)lookup_addr;
            trace_write(TRACE_REGRD(regaddr, data));
        }

        if (multicore_fifo_rvalid())
        {
            ide_phy_msg_t *buf = (ide_phy_msg_t*)sio_hw->fifo_rd;
            if (buf->type == IDE_MSG_CMD_DONE)
            {
                g_ide_registers[REGRD_ERROR] = buf->payload.cmd_done.error;

                if (buf->payload.cmd_done.error != 0)
                {
                    ide_set_status(IDE_STATUS_DEVRDY | IDE_STATUS_ERR);
                }
                else
                {
                    ide_set_status(IDE_STATUS_DEVRDY);
                }
            }
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
    uint32_t msg = 0;
    while ((msg = trace_read()) != 0)
    {
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
        else if (id == TRACE_ID_FIFO_OVERRUN)
        {
            azdbg("-- PHY RX FIFO OVERRUN");
        }
    }
}

void ide_phy_reset()
{
    multicore_reset_core1();
    multicore_fifo_drain();
    ide_phy_init();
    multicore_launch_core1_with_stack(&ide_phy_loop_core1, g_ide_phy_core1_stack, sizeof(g_ide_phy_core1_stack));
}

ide_phy_msg_t *ide_phy_get_msg()
{
    if (g_azlog_debug)
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
    if (multicore_fifo_wready())
    {
        ide_phy_msg_t *buf = ide_phy_get_sndbuf();
        memcpy(buf, msg, sizeof(ide_phy_msg_t));
        sio_hw->fifo_wr = (uint32_t)buf;
        return true;
    }
    else
    {
        return false;
    }
}
