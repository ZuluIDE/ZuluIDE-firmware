/**
 * ZuluIDE™ - Copyright (c) 2026 Rabbit Hole Computing™
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Under Section 7 of GPL version 3, you are granted additional
 * permissions described in the ZuluIDE Hardware Support Library Exception
 * (GPL-3.0_HSL_Exception.md), as published by Rabbit Hole Computing™.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
**/

#include "ide_security_log.h"
#include "ZuluIDE_log.h"
#include "ZuluIDE.h"

// Forward declaration — defined in ZuluIDE.cpp
extern void save_logfile(bool always);

bool log_security_event(ide_registers_t *regs, const uint8_t *password_data, size_t password_len)
{
    uint8_t opcode = regs->command;
    const char *event = nullptr;
    switch (regs->command)
    {
        case IDE_CMD_SECURITY_SET_PASSWORD: event = "SET_PASSWORD"; break;
        case IDE_CMD_SECURITY_UNLOCK: event = "UNLOCK"; break;
        case IDE_CMD_SECURITY_ERASE_PREPARE: event = "ERASE_PREPARE"; break;
        case IDE_CMD_SECURITY_FREEZE_LOCK: event = "FREEZE_LOCK"; break;
        case IDE_CMD_SECURITY_DISABLE_PASSWORD: event = "DISABLE_PASSWORD"; break;
        default: event = "UNKNOWN_SECURITY_CMD"; break;
    }

    dbgmsg("[SECURITY] ", event,
            " opcode=0x", (uint16_t)opcode,
            " feat=0x", (uint16_t)regs->feature,
            " sc=0x", (uint16_t)regs->sector_count,
            " lba=0x", (uint32_t)((regs->lba_high << 16) | (regs->lba_mid << 8) | regs->lba_low),
            " dev=0x", (uint16_t)regs->device);


    if (password_data && password_len > 0)
    {
        // Print password bytes as hex, space-separated
        char hexbuf[96] = {0};
        size_t hexlen = 0;
        const char *nibble = "0123456789ABCDEF";
        for (size_t i = 0; i < password_len && hexlen < sizeof(hexbuf) - 4; i++)
        {
            hexbuf[hexlen++] = nibble[(password_data[i] >> 4) & 0xF];
            hexbuf[hexlen++] = nibble[password_data[i] & 0xF];
            hexbuf[hexlen++] = ' ';
        }
        if (hexlen > 0 && hexbuf[hexlen - 1] == ' ') hexlen--;
        hexbuf[hexlen] = '\0';
        dbgmsg("[SECURITY]   data: ", hexbuf);
    }

    // Flush to SD card immediately so the entry survives any subsequent
    // reset triggered by the host right after a security command.
    save_logfile(true);

    ide_phy_assert_irq(IDE_STATUS_DEVRDY | IDE_STATUS_DSC);
    return true;
}
