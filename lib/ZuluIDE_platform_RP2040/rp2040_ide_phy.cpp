#include <ide_phy.h>

void ide_phy_reset_from_watchdog() {}
void ide_phy_reset(bool has_dev0, bool has_dev1) {}
ide_phy_msg_t *ide_phy_get_msg() { return 0; }
bool ide_phy_send_msg(ide_phy_msg_t *msg) { return true; }
