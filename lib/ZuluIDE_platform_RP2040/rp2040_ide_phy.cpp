#include <ide_phy.h>
#include "rp2040_fpga.h"
#include <assert.h>
#include <ZuluIDE_log.h>

static struct {
    ide_phy_config_t config;
    uint32_t blocklen;
    bool transfer_running;
} g_ide_phy;

// Reset the IDE phy
void ide_phy_reset(const ide_phy_config_t* config)
{
    g_ide_phy.config = *config;

    fpga_init();

    uint8_t cfg = 0;
    if (config->enable_dev0)       cfg |= 1;
    if (config->enable_dev1)       cfg |= 2;
    if (config->enable_dev1_zeros) cfg |= 4;
    fpga_wrcmd(FPGA_CMD_SET_IDE_PHY_CFG, &cfg, 1);
}

void ide_phy_reset_from_watchdog()
{
    ide_phy_reset(&g_ide_phy.config);
}

// Poll for new events.
// Returns IDE_EVENT_NONE if no new events.
ide_event_t ide_phy_get_events()
{
    static uint8_t pending_status = 0;
    uint8_t status;
    fpga_rdcmd(FPGA_CMD_READ_STATUS, &status, 1);

    pending_status |= status;
    if (pending_status & FPGA_STATUS_IDE_RST)
    {
        pending_status &= ~FPGA_STATUS_IDE_RST;
        return IDE_EVENT_HWRST;
    }
    else if (pending_status & FPGA_STATUS_IDE_CMD)
    {
        pending_status &= ~FPGA_STATUS_IDE_CMD;
        return IDE_EVENT_CMD;
    }
    else if (g_ide_phy.transfer_running)
    {
        if (status & FPGA_STATUS_DATA_DIR)
        {
            if (status & FPGA_STATUS_TX_DONE)
            {
                g_ide_phy.transfer_running = false;
                return IDE_EVENT_DATA_TRANSFER_DONE;
            }
        }
        else
        {
            if (status & FPGA_STATUS_RX_DONE)
            {
                g_ide_phy.transfer_running = false;
                return IDE_EVENT_DATA_TRANSFER_DONE;
            }
        }
    }
    
    return IDE_EVENT_NONE;
}

// Get current state of IDE registers
void ide_phy_get_regs(ide_registers_t *regs)
{
    fpga_rdcmd(FPGA_CMD_READ_IDE_REGS, (uint8_t*)regs, sizeof(regs));
}

// Set current state of IDE registers
void ide_phy_set_regs(const ide_registers_t *regs)
{
    fpga_wrcmd(FPGA_CMD_WRITE_IDE_REGS, (const uint8_t*)regs, sizeof(regs));
}

// Data writes to IDE bus
void ide_phy_start_write(uint32_t blocklen)
{
    g_ide_phy.blocklen = blocklen;
    uint16_t arg = blocklen - 1;
    fpga_wrcmd(FPGA_CMD_START_WRITE, (const uint8_t*)&arg, 2);
}

bool ide_phy_can_write_block()
{
    uint8_t status;
    fpga_rdcmd(FPGA_CMD_READ_STATUS, &status, 1);
    assert(status & FPGA_STATUS_DATA_DIR);
    return (status & FPGA_STATUS_TX_CANWRITE);
}

void ide_phy_write_block(const uint8_t *buf)
{
    fpga_wrcmd(FPGA_CMD_WRITE_DATABUF, buf, g_ide_phy.blocklen);
    g_ide_phy.transfer_running = true;
}

bool ide_phy_is_write_finished()
{
    uint8_t status;
    fpga_rdcmd(FPGA_CMD_READ_STATUS, &status, 1);
    assert(status & FPGA_STATUS_DATA_DIR);
    return (status & FPGA_STATUS_TX_DONE);
}

void ide_phy_start_read(uint32_t blocklen)
{
    g_ide_phy.blocklen = blocklen;
    uint16_t arg = blocklen - 1;
    fpga_wrcmd(FPGA_CMD_START_WRITE, (const uint8_t*)&arg, 2);
    g_ide_phy.transfer_running = true;
}

bool ide_phy_can_read_block()
{
    uint8_t status;
    fpga_rdcmd(FPGA_CMD_READ_STATUS, &status, 1);
    assert(!(status & FPGA_STATUS_DATA_DIR));
    return (status & FPGA_STATUS_RX_DONE);
}

void ide_phy_read_block(uint8_t *buf)
{
    fpga_rdcmd(FPGA_CMD_READ_DATABUF, buf, g_ide_phy.blocklen);
}

void ide_phy_stop_transfers()
{
    // Configure buffer in write mode but don't write any data => transfer stopped
    g_ide_phy.blocklen = 0;
    uint16_t arg = 65535;
    fpga_wrcmd(FPGA_CMD_START_WRITE, (const uint8_t*)&arg, 2);
}

// Assert IDE interrupt and set status register
void ide_phy_assert_irq(uint8_t ide_status)
{
    fpga_wrcmd(FPGA_CMD_ASSERT_IRQ, &ide_status, 1);
}
