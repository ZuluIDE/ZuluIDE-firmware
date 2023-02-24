// High-level implementation of IDE command handling

#include "ZuluIDE_log.h"
#include "ide_phy.h"
#include "ide_constants.h"

static const char *get_ide_command_name(uint8_t cmd)
{
    switch (cmd)
    {
#define CMD_NAME_TO_STR(x) case (IDE_CMD_ ## x): return #x;
    IDE_COMMAND_LIST(CMD_NAME_TO_STR)
#undef CMD_NAME_TO_STR
        default: return "UNKNOWN_CMD";
    }
}

static void ide_handle_command(ide_phy_msg_t *msg)
{
    ide_phy_msg_t response = {};
    uint8_t cmd = msg->payload.cmd_start.command;

    azdbg("-- Command: ", cmd, " ", get_ide_command_name(cmd));

    response.type = IDE_MSG_CMD_DONE;
    response.payload.cmd_done.error = IDE_ERROR_ABORT;
    ide_phy_send_msg(&response);
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
            ide_handle_command(msg);
        }
    }
}