/**
 * ZuluIDE™ - Copyright (c) 2026 (local fork)
 *
 * Local-only implementation. See ide_security_log.h.
**/

#include "ide_security_log.h"
#include "ZuluIDE_log.h"

void log_security_event(const char *event, uint8_t opcode, ide_registers_t *regs,
                        const uint8_t *password_data, size_t password_len)
{
    if (regs)
    {
        logmsg("[SECURITY] ", event,
               " opcode=0x", (uint16_t)opcode,
               " feat=0x", (uint16_t)regs->feature,
               " sc=0x", (uint16_t)regs->sector_count,
               " lba=0x", (uint32_t)((regs->lba_high << 16) | (regs->lba_mid << 8) | regs->lba_low),
               " dev=0x", (uint16_t)regs->device);
    }
    else
    {
        logmsg("[SECURITY] ", event, " opcode=0x", (uint16_t)opcode);
    }

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
        logmsg("[SECURITY]   data: ", hexbuf);
    }

    // Flush to SD card immediately so the entry survives any subsequent
    // reset triggered by the host right after a security command.
    save_logfile(true);
}
