// High-level implementation of IDE command handling

#include "ZuluIDE.h"
#include "ZuluIDE_config.h"
#include "ide_phy.h"
#include "ide_constants.h"
#include "ide_cdrom.h"

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

static uint32_t g_ide_buffer[IDE_BUFFER_SIZE / 4];
static IDECDROMDevice g_ide_cdrom;
static IDEImageFile g_ide_current_image((uint8_t*)g_ide_buffer, IDE_BUFFER_SIZE);
static IDEDevice *g_ide_device = &g_ide_cdrom;

void ide_protocol_init()
{
    ide_phy_reset();

    g_ide_current_image.open_file(SD.vol(), "cd.iso", true);
    g_ide_cdrom.set_image(&g_ide_current_image);
}

void ide_protocol_poll()
{
    ide_phy_msg_t *msg = ide_phy_get_msg();

    if (msg)
    {
        if (msg->type == IDE_MSG_CMD_START)
        {
            uint8_t cmd = msg->payload.cmd_start.command;
            azdbg("Command: ", cmd, " ", get_ide_command_name(cmd));

            bool status = g_ide_device->handle_command(msg);
            if (!status)
            {
                azdbg("-- No command handler");
                ide_phy_msg_t response = {};
                response.type = IDE_MSG_DEVICE_RDY;
                response.payload.device_rdy.error = IDE_ERROR_ABORT;
                if (!ide_phy_send_msg(&response))
                {
                    azlog("-- IDE PHY stuck on command ", cmd, "? Attempting reset");
                    ide_phy_reset();
                }
            }
        }
        else
        {
            switch(msg->type)
            {
                case IDE_MSG_RESET: azdbg("Reset, device control ", msg->payload.reset.device_control); break;
                default: azdbg("PHY EVENT: ", (uint8_t)msg->type); break;
            }

            g_ide_device->handle_event(msg);
        }
    }
}