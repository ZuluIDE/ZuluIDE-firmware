// High-level implementation of IDE command handling

#include "ZuluIDE.h"
#include "ide_protocol.h"
#include "ide_phy.h"
#include "ide_constants.h"

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

static IDEDevice *g_ide_devices[2];

static void do_phy_reset()
{
    ide_phy_reset(g_ide_devices[0] != NULL, g_ide_devices[1] != NULL);
}

void ide_protocol_init(IDEDevice *primary, IDEDevice *secondary)
{
    g_ide_devices[0] = primary;
    g_ide_devices[1] = secondary;

    do_phy_reset();
}


void ide_protocol_poll()
{
    ide_phy_msg_t *msg = ide_phy_get_msg();

    if (msg)
    {
        if (msg->type == IDE_MSG_CMD_START)
        {
            uint8_t cmd = msg->payload.cmd_start.command;
            int selected_device = (msg->payload.cmd_start.device >> 4) & 1;
            azdbg("DEV", selected_device, " Command: ", cmd, " ", get_ide_command_name(cmd));

            IDEDevice *device = g_ide_devices[selected_device];

            if (!device)
            {
                azdbg("-- Ignoring command for device not present");
                return;
            }

            bool status = device->handle_command(msg);
            if (!status)
            {
                azdbg("-- No command handler");
                ide_phy_msg_t response = {};
                response.type = IDE_MSG_DEVICE_RDY;
                response.payload.device_rdy.error = IDE_ERROR_ABORT;
                response.payload.device_rdy.assert_irq = true;
                if (!ide_phy_send_msg(&response))
                {
                    azlog("-- IDE PHY stuck on command ", cmd, "? Attempting reset");
                    do_phy_reset();
                }
            }
            else
            {
                azdbg("-- Command complete");
            }
        }
        else
        {
            switch(msg->type)
            {
                case IDE_MSG_RESET: azdbg("Reset, device control ", msg->payload.reset.device_control); break;
                default: azdbg("PHY EVENT: ", (uint8_t)msg->type); break;
            }

            if (g_ide_devices[0]) g_ide_devices[0]->handle_event(msg);
            if (g_ide_devices[1]) g_ide_devices[1]->handle_event(msg);
        }
    }
}