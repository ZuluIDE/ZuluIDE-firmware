// IDE command implementations
// These will internally call ide_phy_send_msg() with the results.

#pragma once
#include "ide_phy.h"

bool ide_cmd_identify_device(ide_phy_msg_t *msg);
bool ide_cmd_init_dev_params(ide_phy_msg_t *msg);