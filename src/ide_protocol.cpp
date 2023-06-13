/**
 * ZuluIDE™ - Copyright (c) 2023 Rabbit Hole Computing™
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
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
**/

// High-level implementation of IDE command handling

#include "ZuluIDE.h"
#include "ZuluIDE_config.h"
#include "ide_protocol.h"
#include "ide_phy.h"
#include "ide_constants.h"
#include <minIni.h>

// Map from command index for command name for logging
static const char *get_ide_command_name(uint8_t cmd)
{
    switch (cmd)
    {
#define CMD_NAME_TO_STR(name, code) case code: return #name;
    IDE_COMMAND_LIST(CMD_NAME_TO_STR)
#undef CMD_NAME_TO_STR
        default: return "UNKNOWN_CMD";
    }
}

static ide_phy_config_t g_ide_config;
static IDEDevice *g_ide_devices[2];

static void do_phy_reset()
{
    g_ide_config.enable_dev0 = (g_ide_devices[0] != NULL);
    g_ide_config.enable_dev1 = (g_ide_devices[1] != NULL);
    g_ide_config.enable_dev1_zeros = (g_ide_devices[0] != NULL)
                                    && (g_ide_devices[1] == NULL)
                                    && (g_ide_devices[0]->is_packet_device());
    g_ide_config.atapi_dev0 = (g_ide_devices[0] != NULL) && (g_ide_devices[0]->is_packet_device());
    g_ide_config.atapi_dev1 = (g_ide_devices[1] != NULL) && (g_ide_devices[1]->is_packet_device());

    ide_phy_reset(&g_ide_config);
}

void ide_protocol_init(IDEDevice *primary, IDEDevice *secondary)
{
    g_ide_devices[0] = primary;
    g_ide_devices[1] = secondary;

    if (primary) primary->initialize(0);
    if (secondary) secondary->initialize(1);

    do_phy_reset();
}


void ide_protocol_poll()
{
    ide_event_t evt = ide_phy_get_events();
    
    if (evt != IDE_EVENT_NONE)
    {
        LED_ON();

        if (evt == IDE_EVENT_CMD)
        {
            ide_registers_t regs = {};
            ide_phy_get_regs(&regs);

            uint8_t cmd = regs.command;
            int selected_device = (regs.device >> 4) & 1;
            dbgmsg("IDE Command for DEV", selected_device, ": ", cmd, " ", get_ide_command_name(cmd),
                " (device ", regs.device,
                ", dev_ctrl ", regs.device_control,
                ", feature ", regs.feature,
                ", sector_count ", regs.sector_count,
                ", lba ", regs.lba_high, " ", regs.lba_mid, " ", regs.lba_low, ")"
                );

            IDEDevice *device = g_ide_devices[selected_device];

            if (!device)
            {
                dbgmsg("-- Ignoring command for device not present");
                return;
            }

            bool status = device->handle_command(&regs);
            if (!status)
            {
                logmsg("-- Command handler failed for ", get_ide_command_name(cmd));
                regs.error = IDE_ERROR_ABORT;
                ide_phy_set_regs(&regs);
                ide_phy_assert_irq(IDE_STATUS_DEVRDY | IDE_STATUS_ERR);
            }
            else
            {
                dbgmsg("-- Command complete");
            }
        }
        else
        {
            switch(evt)
            {
                case IDE_EVENT_HWRST: dbgmsg("IDE_EVENT_HWRST"); break;
                case IDE_EVENT_SWRST: dbgmsg("IDE_EVENT_SWRST"); break;
                case IDE_EVENT_DATA_TRANSFER_DONE: dbgmsg("IDE_EVENT_DATA_TRANSFER_DONE"); break;
                default: dbgmsg("PHY EVENT: ", (int)evt); break;
            }

            if (g_ide_devices[0]) g_ide_devices[0]->handle_event(evt);
            if (g_ide_devices[1]) g_ide_devices[1]->handle_event(evt);
        }

        LED_OFF();
    }
}

void IDEDevice::initialize(int devidx)
{
    memset(&m_devconfig, 0, sizeof(m_devconfig));

    m_phy_caps = *ide_phy_get_capabilities();
    m_devconfig.max_pio_mode = ini_getl("IDE", "max_pio", 2, CONFIGFILE);
    m_devconfig.max_udma_mode = ini_getl("IDE", "max_udma", -1, CONFIGFILE);
    m_devconfig.max_blocksize = ini_getl("IDE", "max_blocksize", m_phy_caps.max_blocksize, CONFIGFILE);

    logmsg("Device ", devidx, " configuration:");
    logmsg("-- Max PIO mode: ", m_devconfig.max_pio_mode, " (phy max ", m_phy_caps.max_pio_mode, ")");
    logmsg("-- Max UDMA mode: ", m_devconfig.max_udma_mode, " (phy max ", m_phy_caps.max_udma_mode, ")");
    logmsg("-- Max blocksize: ", m_devconfig.max_blocksize, " (phy max ", (int)m_phy_caps.max_blocksize, ")");

    m_phy_caps.max_udma_mode = std::min(m_phy_caps.max_udma_mode, m_devconfig.max_udma_mode);
    m_phy_caps.max_pio_mode = std::min(m_phy_caps.max_pio_mode, m_devconfig.max_pio_mode);
    m_phy_caps.max_blocksize = std::min<int>(m_phy_caps.max_blocksize, m_devconfig.max_blocksize);
}