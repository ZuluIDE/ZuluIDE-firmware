// High-level implementation of IDE command handling

#include "ZuluIDE_log.h"
#include "ide_phy.h"
#include "ide_constants.h"
#include "ide_commands.h"

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

static bool ide_handle_command(ide_phy_msg_t *msg)
{
    ide_phy_msg_t response = {};
    uint8_t cmd = msg->payload.cmd_start.command;

    azdbg("-- Command: ", cmd, " ", get_ide_command_name(cmd));

    switch (cmd)
    {
        case IDE_CMD_IDENTIFY_DEVICE: return ide_cmd_identify_device(msg);
        case IDE_CMD_INIT_DEV_PARAMS: return ide_cmd_init_dev_params(msg);

        default:
            azdbg("-- No handler for command, reporting error");
            response.type = IDE_MSG_CMD_DONE;
            response.payload.cmd_done.error = IDE_ERROR_ABORT;
            return ide_phy_send_msg(&response);
    }
}


void ide_protocol_init()
{
    ide_phy_reset();
}

void ide_protocol_poll()
{
    ide_phy_msg_t *msg = ide_phy_get_msg();

    if (msg)
    {
        if (msg->type == IDE_MSG_CMD_START)
        {
            bool status = ide_handle_command(msg);
            if (!status)
            {
                azdbg("Command handling problem");
            }
        }
    }
}