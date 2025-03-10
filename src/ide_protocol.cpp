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
 * Under Section 7 of GPL version 3, you are granted additional
 * permissions described in the ZuluIDE Hardware Support Library Exception
 * (GPL-3.0_HSL_Exception.md), as published by Rabbit Hole Computing™.
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

// ATA/ATAPI 6 Device 0 state machine section 9.10 - T13 1410D
enum exec_dev_diag_state_t
{
    EXEC_DEV_DIAG_STATE_IDLE = 0,
    EXEC_DEV_DIAG_STATE_WAIT,
    EXEC_DEV_DIAG_STATE_SAMPLE,
    EXEC_DEV_DIAG_STATE_SET_STATUS

};

static exec_dev_diag_state_t g_exec_dev_diag_state;
static ide_phy_config_t g_ide_config;
static IDEDevice *g_ide_devices[2];
static ide_event_t g_last_reset_event;
static uint32_t g_last_reset_time;
static bool g_drive1_detected;
uint8_t g_ide_signals;
static uint32_t g_last_event_time;
static ide_event_t g_last_event;
static ide_registers_t g_prev_ide_regs;
static bool g_ide_reset_after_init_done;

bool g_ignore_cmd_interrupt;

static void do_phy_reset()
{
    if (g_ide_config.enable_dev0 && !g_ide_config.enable_dev1)
    {
        bool force_drive1 = ini_getbool("IDE", "has_drive1", false, CONFIGFILE);
        bool force_no_drive1 = !ini_getbool("IDE", "has_drive1", true, CONFIGFILE);

        if (force_drive1)
        {
            dbgmsg("-- Config has_drive1=1, forcing second drive presence");
            g_drive1_detected = true;
        }
        else if (force_no_drive1)
        {
            dbgmsg("-- Config has_drive1=0, forcing second drive absence");
            g_drive1_detected = false;
        }
    }

    g_ide_config.enable_dev0 = (g_ide_devices[0] != NULL);
    g_ide_config.enable_dev1 = (g_ide_devices[1] != NULL);
    g_ide_config.enable_dev1_zeros = (g_ide_devices[0] != NULL)
                                    && (g_ide_devices[1] == NULL)
                                    && (g_ide_devices[0]->is_packet_device())
                                    && !g_drive1_detected;
    g_ide_config.atapi_dev0 = (g_ide_devices[0] != NULL) && (g_ide_devices[0]->is_packet_device());
    g_ide_config.atapi_dev1 = (g_ide_devices[1] != NULL) && (g_ide_devices[1]->is_packet_device());
    // \todo if the code base support two devices, make `disable_iordy` a per device setting
    g_ide_config.disable_iordy = ((g_ide_devices[0] != NULL) && (g_ide_devices[0]->disables_iordy()))
                                 || ((g_ide_devices[1] != NULL) && (g_ide_devices[1]->disables_iordy()));
    g_ide_config.enable_packet_intrq = ini_getbool("IDE", "atapi_intrq", 0, CONFIGFILE);

    if (g_ide_config.enable_dev0 && !g_ide_config.enable_dev1)
    {
        if (g_drive1_detected)
        {
            dbgmsg("-- Operating as primary drive, secondary drive detected");
        }
        else
        {
            dbgmsg("-- Operating as primary drive, secondary drive not detected");
        }
    }
    else if (g_ide_config.enable_dev1 && !g_ide_config.enable_dev0)
    {
        dbgmsg("-- Operating as secondary drive");
    }
    else
    {
        dbgmsg("-- Operating as two drives");
    }

    ide_phy_reset(&g_ide_config);
}

const ide_phy_config_t *ide_protocol_get_config()
{
    return &g_ide_config;
}

void ide_protocol_init(IDEDevice *primary, IDEDevice *secondary)
{
    g_ide_devices[0] = primary;
    g_ide_devices[1] = secondary;

    if (primary) primary->initialize(0);
    if (secondary) secondary->initialize(1);

    do_phy_reset();
    g_ide_reset_after_init_done = false;
}


void ide_protocol_poll()
{
    ide_event_t evt = ide_phy_get_events();
    ide_registers_t regs = {};

    if (!g_ide_reset_after_init_done)
    {
        evt = IDE_EVENT_HWRST;
        g_ide_reset_after_init_done = true;
    }

    if (evt != IDE_EVENT_NONE)
    {
        LED_ON();

        if (evt == IDE_EVENT_CMD)
        {
            ide_phy_get_regs(&regs);

            uint8_t cmd = regs.command;
            if (cmd == IDE_CMD_EXECUTE_DEVICE_DIAGNOSTIC)
            {
                ide_phy_set_signals(0);
                regs.device &= ~IDE_DEVICE_DEV;
                ide_phy_set_regs(&regs);
                g_last_event_time = millis();
                g_last_event = IDE_EVENT_CMD_EXE_DEV_DIAG;
                // Drive 0 is the current drive and drive 1 is detected
                if (g_ide_config.enable_dev0 && (g_ide_config.enable_dev1 || g_drive1_detected))
                        g_exec_dev_diag_state = EXEC_DEV_DIAG_STATE_WAIT;
                // Drive 0 the is current drive and no drive 1 is detected
                else if (g_ide_config.enable_dev0 && !(g_ide_config.enable_dev1 || g_drive1_detected))
                        g_exec_dev_diag_state = EXEC_DEV_DIAG_STATE_SET_STATUS;

                dbgmsg("IDE Command: ", cmd, " ", get_ide_command_name(cmd),
                    " (device ", regs.device,
                    ", dev_ctrl ", regs.device_control,
                    ", feature ", regs.feature,
                    ", sector_count ", regs.sector_count,
                    ", lba ", regs.lba_high, " ", regs.lba_mid, " ", regs.lba_low, ")"
                    );

                return;
            }

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
                dbgmsg("-- Command was for a device that is not present - reporting failure");
                regs.error = IDE_ERROR_ABORT;
                ide_phy_set_regs(&regs);
                ide_phy_assert_irq(IDE_STATUS_DEVRDY | IDE_STATUS_DSC | IDE_STATUS_ERR);
                return;
            }

            regs.error = 0;
            ide_phy_set_signals(g_ide_signals | IDE_SIGNAL_DASP); // Set motherboard IDE status led
            bool status = device->handle_command(&regs);
            ide_phy_set_signals(g_ide_signals);

            if (!status)
            {
                logmsg("-- Command handler failed for ", get_ide_command_name(cmd));
                regs.error = IDE_ERROR_ABORT;
                ide_phy_set_regs(&regs);
                ide_phy_assert_irq(IDE_STATUS_DEVRDY | IDE_STATUS_DSC | IDE_STATUS_ERR);
            }
            // \todo figure out if possibly reading the status from the FPGA is causing
            // `ide_phy_is_command_interrupted` to crash the board on certain IDE controllers
            // else if (ide_phy_is_command_interrupted())
            // {
            //     logmsg("-- Command was interrupted");
            //     regs.error = IDE_ERROR_ABORT;
            //     ide_phy_set_regs(&regs);
            //     ide_phy_assert_irq(IDE_STATUS_DEVRDY | IDE_STATUS_ERR);
            // }
            else if (regs.error)
            {
                dbgmsg("-- Command ", get_ide_command_name(cmd), " completed with error status ", regs.error);
            }
            else
            {
                dbgmsg("-- Command complete");
            }
        }
        else
        {
            if (evt != g_last_event || (millis() - g_last_event_time) > 5000)
            {
                switch(evt)
                {
                    case IDE_EVENT_HWRST: dbgmsg("IDE_EVENT_HWRST"); break;
                    case IDE_EVENT_SWRST: dbgmsg("IDE_EVENT_SWRST"); break;
                    case IDE_EVENT_DATA_TRANSFER_DONE: dbgmsg("IDE_EVENT_DATA_TRANSFER_DONE"); break;
                    default: dbgmsg("PHY EVENT: ", (int)evt); break;
                }
            }

            if (evt == IDE_EVENT_HWRST || evt == IDE_EVENT_SWRST)
            {
                // \todo move to a reset or init function
                g_exec_dev_diag_state = EXEC_DEV_DIAG_STATE_IDLE;

                if (g_ide_devices[1])
                {
                     // Clear DEV bit in device reg within 1ms if secondary device
                    ide_phy_get_regs(&regs);
                    regs.device &= ~IDE_DEVICE_DEV;
                    ide_phy_set_regs(&regs);
                }

                g_ide_signals = 0;
                ide_phy_set_signals(0); // Release DASP and PDIAG
                g_last_reset_time = millis();
                g_last_reset_event = evt;

                if (evt == IDE_EVENT_HWRST)
                {
                    // Some drives don't assert DASP after SWRST,
                    // keep result from latest HWRST.
                    g_drive1_detected = false;
                }
                if (g_ide_devices[0]) g_ide_devices[0]->reset();
                if (g_ide_devices[1]) g_ide_devices[1]->reset();
            }

            if (g_ide_devices[0]) g_ide_devices[0]->handle_event(evt);
            if (g_ide_devices[1]) g_ide_devices[1]->handle_event(evt);
        }

        LED_OFF();
        g_last_event_time = millis();
        g_last_event = evt;
    }
    else if ((millis() - g_last_event_time) > 10)
    {
        // Log any changes in IDE registers
        g_last_event_time = millis();
        ide_registers_t regs = {};
        ide_phy_get_regs(&regs);

        if (memcmp(&regs, &g_prev_ide_regs, sizeof(ide_registers_t)) != 0)
        {
            g_prev_ide_regs = regs;
            dbgmsg("-- IDE regs:",
                " STATUS:", regs.status,
                " CMD:", regs.command,
                " DEV:", regs.device,
                " DEVCTRL:", regs.device_control,
                " ERROR:", regs.error,
                " FEATURE:", regs.feature,
                " LBAL:", regs.lba_low,
                " LBAM:", regs.lba_mid,
                " LBAH:", regs.lba_high);
        }

        if (regs.status & IDE_STATUS_BSY)
        {
            // This can happen if the host does unexpected DATA register access.
            // Because we are not executing a command, status should be DRDY.
            dbgmsg("---- Clearing IDE busy status");
            regs.status = 0x50;
            ide_phy_set_regs(&regs);
        }
    }

    if (g_last_reset_event == IDE_EVENT_HWRST || g_last_reset_event == IDE_EVENT_SWRST)
    {
        uint32_t time_passed = millis() - g_last_reset_time;
        if (g_ide_devices[1])
        {
            // Announce our presence to primary device
            if (time_passed > 5 && g_ide_signals == 0)
            {
                // Assert DASP to indicate presence
                g_ide_signals = IDE_SIGNAL_DASP;
                ide_phy_set_signals(g_ide_signals);
            }
            else if (time_passed > 100 && g_ide_signals == IDE_SIGNAL_DASP)
            {
                ide_phy_get_regs(&regs);
                g_ide_devices[1]->fill_device_signature(&regs);
                regs.status &= ~(IDE_STATUS_ERR | IDE_STATUS_CORR | IDE_STATUS_DATAREQ);
                if (g_ide_devices[1]->is_packet_device())
                    regs.status &= ~(IDE_STATUS_SERVICE | IDE_STATUS_DEVFAULT | IDE_STATUS_DEVRDY | IDE_STATUS_BSY);
                regs.device_control = 0; // Should just reset IDE_DEVCTRL_SRST, but IDE_CMD_PACKET fails if it isn't cleared
                ide_phy_set_regs(&regs);

                // Assert PDIAG to indicate passed diagnostics
                g_ide_signals = IDE_SIGNAL_DASP | IDE_SIGNAL_PDIAG;
                ide_phy_set_signals(g_ide_signals);
            }
            else if (time_passed > 31000 || evt == IDE_EVENT_CMD)
            {
                // Release DASP after first command or 31 secs
                g_ide_signals = IDE_SIGNAL_PDIAG;
                ide_phy_set_signals(g_ide_signals);
                g_last_reset_event = IDE_EVENT_NONE;

            }
        }
        else if (g_last_reset_event == IDE_EVENT_HWRST)
        {
            // Monitor presence of secondary device
            uint8_t signals = ide_phy_get_signals();

            if (signals & IDE_SIGNAL_DASP)
            {
                g_drive1_detected = true;
            }

            if (time_passed > 500)
            {
                // Apply configuration based on whether drive1 was present
                do_phy_reset();
                g_last_reset_event = IDE_EVENT_NONE;
            }
        }
    }

    if (g_last_event == IDE_EVENT_CMD_EXE_DEV_DIAG)
    {
        static bool sample_state_happened = false;
        ide_registers_t regs = {0};
        uint32_t time_passed = millis() - g_last_event_time;

        if (EXEC_DEV_DIAG_STATE_WAIT == g_exec_dev_diag_state && time_passed > 1)
            g_exec_dev_diag_state = EXEC_DEV_DIAG_STATE_SAMPLE;
        else if (EXEC_DEV_DIAG_STATE_SAMPLE == g_exec_dev_diag_state)
        {
            sample_state_happened = true;
            bool pdiag = !!(ide_phy_get_signals() & IDE_SIGNAL_PDIAG);
            if (pdiag)
            {
                ide_phy_get_regs(&regs);
                regs.error = 0;
                regs.error &= ~IDE_ERROR_EXEC_DEV_DIAG_DEV1_FAIL; // unset bit, DEV 1 passed
                g_exec_dev_diag_state = EXEC_DEV_DIAG_STATE_SET_STATUS;
            }
            // dev1 self test failed after 6 seconds
            else if (time_passed > 6000)
            {
                ide_phy_get_regs(&regs);
                regs.error = 0;
                regs.error |= IDE_ERROR_EXEC_DEV_DIAG_DEV1_FAIL; // set bit, DEV 1 failed
                g_exec_dev_diag_state = EXEC_DEV_DIAG_STATE_SET_STATUS;
            }
        }
        // no need to poll again if the state changed to EXEC_DEV_DIAG_STATE_SAMPLE
        if (EXEC_DEV_DIAG_STATE_SET_STATUS == g_exec_dev_diag_state)
        {
            if (sample_state_happened)
            {
                sample_state_happened = false;
            }
            else
            {
                ide_phy_get_regs(&regs);
                regs.error = 0;
            }

            // device 0 passed
            regs.error |= IDE_ERROR_EXEC_DEV_DIAG_DEV0_PASS;
            regs.device &= ~IDE_DEVICE_DEV;
            g_ide_devices[0]->fill_device_signature(&regs);
            regs.status &= ~(IDE_STATUS_ERR | IDE_STATUS_CORR | IDE_STATUS_DATAREQ);
            if (g_ide_devices[0]->is_packet_device())
                regs.status &= ~(IDE_STATUS_SERVICE | IDE_STATUS_DEVFAULT | IDE_STATUS_DEVRDY);
            g_exec_dev_diag_state = EXEC_DEV_DIAG_STATE_IDLE;
            g_last_event = IDE_EVENT_NONE;

            ide_phy_set_regs(&regs);

            regs.status &= ~IDE_STATUS_BSY;
            ide_phy_set_regs(&regs);
            ide_phy_assert_irq(IDE_STATUS_DEVRDY | IDE_STATUS_DSC | regs.status);
        }
    }
}

void IDEDevice::formatDriveInfoField(char *field, int fieldsize, bool align_right)
{
    if (align_right)
    {
        // Right align and trim spaces on either side
        int dst = fieldsize - 1;
        for (int src = fieldsize - 1; src >= 0; src--)
        {
            char c = field[src];
            if (c < 0x20 || c > 0x7E) c = 0x20;
            if (c != 0x20 || dst != fieldsize - 1)
            {
                field[dst--] = c;
            }
        }
        while (dst >= 0)
        {
            field[dst--] = 0x20;
        }
    }
    else
    {
        // Left align, preserve spaces in case config tries to manually right-align
        int dst = 0;
        for (int src = 0; src < fieldsize; src++)
        {
            char c = field[src];
            if (c < 0x20 || c > 0x7E) c = 0x20;
            field[dst++] = c;
        }
        while (dst < fieldsize)
        {
            field[dst++] = 0x20;
        }
    }
}

void IDEDevice::set_ident_strings(const char* default_model, const char* default_serial, const char* default_revision)
{
    char input_str[41];
    uint16_t input_len;

    memset(input_str, '\0', 41);
    input_len = ini_gets("IDE", "ide_model", default_model, input_str, 41, CONFIGFILE);
    if (input_len > 40) input_len = 40;
    memcpy(m_devconfig.ata_model, input_str, input_len);
    formatDriveInfoField(m_devconfig.ata_model, 40, false);

    memset(input_str, '\0', 21);
    input_len = ini_gets("IDE","ide_serial", default_serial, input_str, 21, CONFIGFILE);
    if (input_len > 20) input_len = 20;
    memcpy(m_devconfig.ata_serial, input_str, input_len);
    formatDriveInfoField(m_devconfig.ata_serial, 20, false);

    memset(input_str, '\0', 9);
    input_len = ini_gets("IDE","ide_revision", default_revision, input_str, 9, CONFIGFILE);
    if (input_len > 8) input_len = 8;
    memcpy(m_devconfig.ata_revision, input_str, input_len);
    formatDriveInfoField(m_devconfig.ata_revision, 8, false);

}

void IDEDevice::initialize(int devidx)
{
    memset(&m_devconfig, 0, sizeof(m_devconfig));

    m_devconfig.dev_index = devidx;

    m_phy_caps = *ide_phy_get_capabilities();
    m_devconfig.max_pio_mode = ini_getl("IDE", "max_pio", 3, CONFIGFILE);
    m_devconfig.max_udma_mode = ini_getl("IDE", "max_udma", 0, CONFIGFILE);
    m_devconfig.max_blocksize = ini_getl("IDE", "max_blocksize", m_phy_caps.max_blocksize, CONFIGFILE);
    logmsg("Device ", devidx, " configuration:");
    logmsg("-- Max PIO mode: ", m_devconfig.max_pio_mode, " (phy max ", m_phy_caps.max_pio_mode, ")");
    logmsg("-- Max UDMA mode: ", m_devconfig.max_udma_mode, " (phy max ", m_phy_caps.max_udma_mode, ")");
    logmsg("-- Max blocksize: ", m_devconfig.max_blocksize, " (phy max ", (int)m_phy_caps.max_blocksize, ")");
    m_devconfig.ide_sectors = ini_getl("IDE", "sectors", 0, CONFIGFILE);
    m_devconfig.ide_heads = ini_getl("IDE", "heads", 0, CONFIGFILE);
    m_devconfig.ide_cylinders = ini_getl("IDE", "cylinders", 0, CONFIGFILE);
    m_devconfig.access_delay = ini_getl("IDE", "access_delay", 0, CONFIGFILE);

    g_ignore_cmd_interrupt = ini_getl("IDE", "ignore_command_interrupt", 1, CONFIGFILE);
    if (!g_ignore_cmd_interrupt)
    {
        logmsg("-- New commands may interrupt previous command - ignore_command_interrupt set to 0");
    }
    m_phy_caps.max_udma_mode = std::min(m_phy_caps.max_udma_mode, m_devconfig.max_udma_mode);
    m_phy_caps.max_pio_mode = std::min(m_phy_caps.max_pio_mode, m_devconfig.max_pio_mode);
    m_phy_caps.max_blocksize = std::min<int>(m_phy_caps.max_blocksize, m_devconfig.max_blocksize);
}
