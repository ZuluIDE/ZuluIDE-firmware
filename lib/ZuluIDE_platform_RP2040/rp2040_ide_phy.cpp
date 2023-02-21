// This file implements interface to IDE / Parallel ATA bus.
// It runs on the second core of RP2040.

#include "ZuluIDE_platform.h"
#include "ZuluIDE_config.h"
#include "ZuluIDE_log.h"
#include <hardware/gpio.h>
#include <hardware/pio.h>
#include <hardware/structs/iobank0.h>
#include <multicore.h>
#include <assert.h>
#include "ide_phy.pio.h"

// Messages passed between core 0 and core 1
#define MSG_ID_RESET   0x01
#define MSG_ID_REGWR   0x10
#define MSG_ID_REGRD   0x11

#define MSG_REGWR(addr, data) ((uint32_t)MSG_ID_REGWR | (((addr) & 0xFF) << 8) | (((data) & 0xFFFF) << 16))
#define MSG_REGRD(addr, data) ((uint32_t)MSG_ID_REGRD | (((addr) & 0xFF) << 8) | (((data) & 0xFFFF) << 16))

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

// Helper macro to construct bit mask from bit index
#define BM(x) (1UL << (x))

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

struct ide_registers_t {
    uint8_t device_control;
    uint8_t status;
    uint8_t command;
    uint8_t features;
    uint8_t error;
    uint8_t sector_count;
    uint8_t sector_number;
    uint8_t drive_head;
    union {
        uint8_t bytes[4];
        uint32_t value;
    } lba;
};

static struct {
    ide_registers_t registers;

    bool channels_claimed;
    uint32_t pio_offset_ide_reg_write;
    pio_sm_config pio_cfg_ide_reg_write;
    uint32_t pio_offset_ide_reg_read;
    pio_sm_config pio_cfg_ide_reg_read;
} g_ide_phy;

static uint32_t g_ide_phy_core1_stack[512];

static void ide_phy_init()
{
    if (!g_ide_phy.channels_claimed)
    {
        pio_sm_claim(IDE_PIO, IDE_PIO_SM_REGWR);
        pio_sm_claim(IDE_PIO, IDE_PIO_SM_REGRD);
        g_ide_phy.channels_claimed = true;
    }

    g_ide_phy.pio_offset_ide_reg_write = pio_add_program(IDE_PIO, &ide_reg_write_program);
    g_ide_phy.pio_cfg_ide_reg_write = ide_reg_write_program_get_default_config(g_ide_phy.pio_offset_ide_reg_write);
    sm_config_set_out_shift(&g_ide_phy.pio_cfg_ide_reg_write, true, false, 32);
    sm_config_set_in_shift(&g_ide_phy.pio_cfg_ide_reg_write, true, true, 32);
    sm_config_set_in_pins(&g_ide_phy.pio_cfg_ide_reg_write, IDE_IO_SHIFT);
    sm_config_set_out_pins(&g_ide_phy.pio_cfg_ide_reg_write, IDE_IO_SHIFT, 16);
    sm_config_set_sideset_pins(&g_ide_phy.pio_cfg_ide_reg_write, MUX_SEL);

    g_ide_phy.pio_offset_ide_reg_read = pio_add_program(IDE_PIO, &ide_reg_read_program);
    g_ide_phy.pio_cfg_ide_reg_read = ide_reg_read_program_get_default_config(g_ide_phy.pio_offset_ide_reg_read);
    sm_config_set_out_shift(&g_ide_phy.pio_cfg_ide_reg_read, true, false, 32);
    sm_config_set_in_shift(&g_ide_phy.pio_cfg_ide_reg_read, true, true, 32);
    sm_config_set_in_pins(&g_ide_phy.pio_cfg_ide_reg_read, IDE_IO_SHIFT);
    sm_config_set_out_pins(&g_ide_phy.pio_cfg_ide_reg_read, IDE_IO_SHIFT, 16);
    sm_config_set_sideset_pins(&g_ide_phy.pio_cfg_ide_reg_read, MUX_SEL);

    // Initialize register write watcher
    pio_sm_init(IDE_PIO, IDE_PIO_SM_REGWR, g_ide_phy.pio_offset_ide_reg_write, &g_ide_phy.pio_cfg_ide_reg_write);
    pio_sm_put(IDE_PIO, IDE_PIO_SM_REGWR, MUXCR_WRITE);
    pio_sm_put(IDE_PIO, IDE_PIO_SM_REGWR, MUXCR_STATUS);
    pio_sm_exec(IDE_PIO, IDE_PIO_SM_REGWR, pio_encode_pull(false, true));
    pio_sm_exec(IDE_PIO, IDE_PIO_SM_REGWR, pio_encode_mov(pio_x, pio_osr));
    pio_sm_exec(IDE_PIO, IDE_PIO_SM_REGWR, pio_encode_pull(false, true));
    pio_sm_exec(IDE_PIO, IDE_PIO_SM_REGWR, pio_encode_mov(pio_y, pio_osr));

    // Initialize register read watcher
    pio_sm_init(IDE_PIO, IDE_PIO_SM_REGRD, g_ide_phy.pio_offset_ide_reg_read, &g_ide_phy.pio_cfg_ide_reg_read);
    pio_sm_put(IDE_PIO, IDE_PIO_SM_REGRD, MUXCR_READ);
    pio_sm_put(IDE_PIO, IDE_PIO_SM_REGRD, MUXCR_STATUS);
    pio_sm_exec(IDE_PIO, IDE_PIO_SM_REGRD, pio_encode_pull(false, true));
    pio_sm_exec(IDE_PIO, IDE_PIO_SM_REGRD, pio_encode_mov(pio_x, pio_osr));
    pio_sm_exec(IDE_PIO, IDE_PIO_SM_REGRD, pio_encode_pull(false, true));
    pio_sm_exec(IDE_PIO, IDE_PIO_SM_REGRD, pio_encode_mov(pio_y, pio_osr));

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

static inline void send_msg(uint32_t msg)
{
    sio_hw->fifo_wr = msg;
}

__attribute__((section(".scratch_y.idephy")))
static void ide_phy_reset()
{
    // Stop state machines
    pio_sm_set_enabled(IDE_PIO, IDE_PIO_SM_REGWR, false);
    pio_sm_set_enabled(IDE_PIO, IDE_PIO_SM_REGRD, false);

    // Handle bus reset condition
    do {
        gpio_set_pulls(IDE_IN_RST, true, false); // Pull IDERST high to temporarily enable MUX_OE
        ide_phy_write_mux_ctrl(MUXCR_STATUS);
        delay_100ns();
        gpio_set_pulls(IDE_IN_RST, false, true); // Pull IDERST low to see if mux was autodisabled again
        delay_100ns();
    } while (!(sio_hw->gpio_in & BM(IDE_IN_RST)));

    send_msg(MSG_ID_RESET);

    // Start state machines
    pio_sm_clear_fifos(IDE_PIO, IDE_PIO_SM_REGWR);
    pio_sm_clear_fifos(IDE_PIO, IDE_PIO_SM_REGRD);
    pio_sm_set_enabled(IDE_PIO, IDE_PIO_SM_REGWR, true);
    pio_sm_set_enabled(IDE_PIO, IDE_PIO_SM_REGRD, true);
}

__attribute__((section(".scratch_y.idephy")))
static void ide_phy_loop_core1()
{
    ide_phy_reset();

    while (1)
    {
        if (!(sio_hw->gpio_in & BM(IDE_IN_RST)))
        {
            ide_phy_reset();
        }

        uint32_t fstat = ~IDE_PIO->fstat;
        const uint32_t reg_rd_flag = BM(PIO_FSTAT_RXEMPTY_LSB + IDE_PIO_SM_REGRD);
        const uint32_t reg_wr_flag = BM(PIO_FSTAT_RXEMPTY_LSB + IDE_PIO_SM_REGWR);
        if (fstat & reg_wr_flag)
        {
            uint32_t addrdata = pio_sm_get(IDE_PIO, IDE_PIO_SM_REGWR);
            uint32_t addr = addrdata & 0xFFFF;
            uint32_t data = addrdata >> 16;
            send_msg(MSG_REGWR(addr, data));
        }
        else if (fstat & reg_rd_flag)
        {
            uint32_t addr = pio_sm_get(IDE_PIO, IDE_PIO_SM_REGRD);
            pio_sm_put(IDE_PIO, IDE_PIO_SM_REGRD, 0xAA);
            send_msg(MSG_REGRD(addr, 0x00));
        }
    }
}

static void log_reg_write(uint8_t regaddr, uint16_t value)
{
    switch (regaddr & REGADDR_MASK)
    {
        case REGWR_DEVICE_CONTROL: azdbg("-- WR DEVICE_CONTROL <= ", (uint8_t)value); break;
        case REGWR_FEATURE:        azdbg("-- WR FEATURE        <= ", (uint8_t)value); break;
        case REGWR_SECTORCOUNT:    azdbg("-- WR SECTORCOUNT    <= ", (uint8_t)value); break;
        case REGWR_LBALOW:         azdbg("-- WR LBALOW         <= ", (uint8_t)value); break;
        case REGWR_LBAMID:         azdbg("-- WR LBAMID         <= ", (uint8_t)value); break;
        case REGWR_LBAHIGH:        azdbg("-- WR LBAHIGH        <= ", (uint8_t)value); break;
        case REGWR_DEVICE:         azdbg("-- WR DEVICE         <= ", (uint8_t)value); break;
        case REGWR_COMMAND:        azdbg("-- WR COMMAND        <= ", (uint8_t)value); break;
        case REGWR_DATA:           azdbg("-- WR DATA           <= ", value); break;
        default:                   azdbg("-- WR UNKNOWN (", regaddr, ") <= ", value); break;
    }
}

static void log_reg_read(uint8_t regaddr, uint16_t value)
{
    switch (regaddr & REGADDR_MASK)
    {
        case REGRD_ALT_STATUS:     azdbg("-- RD ALT_STATUS     => ", (uint8_t)value); break;
        case REGRD_ERROR:          azdbg("-- RD ERROR          => ", (uint8_t)value); break;
        case REGRD_SECTORCOUNT:    azdbg("-- RD SECTORCOUNT    => ", (uint8_t)value); break;
        case REGRD_LBALOW:         azdbg("-- RD LBALOW         => ", (uint8_t)value); break;
        case REGRD_LBAMID:         azdbg("-- RD LBAMID         => ", (uint8_t)value); break;
        case REGRD_LBAHIGH:        azdbg("-- RD LBAHIGH        => ", (uint8_t)value); break;
        case REGRD_DEVICE:         azdbg("-- RD DEVICE         => ", (uint8_t)value); break;
        case REGRD_STATUS:         azdbg("-- RD STATUS         => ", (uint8_t)value); break;
        case REGRD_DATA:           azdbg("-- RD DATA           => ", value); break;
        default:                   azdbg("-- RD UNKNOWN (", regaddr, ") => ", value); break;
    }
}

static void ide_phy_poll()
{
    while (multicore_fifo_rvalid())
    {
        uint32_t msg = sio_hw->fifo_rd;
        uint8_t id = msg & 0xFF;
        if (id == MSG_ID_RESET)
        {
            azdbg("IDE reset complete");
        }
        else if (id == MSG_ID_REGRD)
        {
            log_reg_read((uint8_t)(msg >> 8), (uint16_t)(msg >> 16));
        }
        else if (id == MSG_ID_REGWR)
        {
            log_reg_write((uint8_t)(msg >> 8), (uint16_t)(msg >> 16));
        }
    }
}

void ide_phy_test()
{
    ide_phy_init();

    multicore_launch_core1_with_stack(&ide_phy_loop_core1, g_ide_phy_core1_stack, sizeof(g_ide_phy_core1_stack));

    while (1)
    {
        ide_phy_poll();
    }
}