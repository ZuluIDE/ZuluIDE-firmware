#include <ZuluIDE_platform.h>
#include "ide_commands.h"
#include "ide_constants.h"

// 0xEC IDE_CMD_IDENTIFY_DEVICE
// Responds with 512 bytes of identification data
bool ide_cmd_identify_device(ide_phy_msg_t *msg)
{
    uint16_t idf[256] = {0};
    volatile ide_msg_status_t status = IDE_MSGSTAT_IDLE;

    idf[IDE_IDENTIFY_OFFSET_NUM_CYLINDERS] = 16;
    idf[IDE_IDENTIFY_OFFSET_NUM_HEADS] = 16;
    idf[IDE_IDENTIFY_OFFSET_BYTES_PER_TRACK] = 64 * 512;
    idf[IDE_IDENTIFY_OFFSET_BYTES_PER_SECTOR] = 512;
    idf[IDE_IDENTIFY_OFFSET_SECTORS_PER_TRACK] = 64;

    ide_phy_msg_t response = {};
    response.status = &status;
    response.type = IDE_MSG_SEND_DATA;
    response.payload.send_data.data = idf;
    response.payload.send_data.words = 256;
    ide_phy_send_msg(&response);

    while (!(status & IDE_MSGSTAT_DONE))
    {
        delay(1);
    }

    response.type = IDE_MSG_CMD_DONE;
    response.payload.cmd_done.error = 0;
    return ide_phy_send_msg(&response);
}

// 0x91 IDE_CMD_INIT_DEV_PARAMS
// Sets drive geometry, obsolete in newer ATA standards.
// Currently implemented as no-op.
bool ide_cmd_init_dev_params(ide_phy_msg_t *msg)
{
    delay(10);

    ide_phy_msg_t response = {};
    response.type = IDE_MSG_ASSERT_IRQ;
    ide_phy_send_msg(&response);

    response.type = IDE_MSG_CMD_DONE;
    response.payload.cmd_done.error = 0;
    return ide_phy_send_msg(&response);
}

