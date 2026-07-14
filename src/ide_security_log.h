/**
 * ZuluIDE™ - Copyright (c) 2026 (local fork)
 *
 * Local-only utility: log ATA security command activity to zululog.txt on the
 * SD card, then flush immediately. This is meant for debugging ATA-Security
 * probing sequences on hosts like the Denso TSC Gen 3/4 nav module.
 *
 * Not for upstream submission — this is a diagnostic aid added on a local
 * branch only. See /CODE/zuluide/README-ata-compliance.md for context.
**/

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <ide_phy.h>

// Force the SD card log buffer to be flushed to zululog.txt right now.
// Defined in ZuluIDE.cpp — declared here so ide_rigid.cpp can call it.
extern void save_logfile(bool always);

// Log an ATA security command event to zululog.txt and flush the log to
// disk immediately. Includes the command opcode, IDE register state, and
// (for SECURITY_UNLOCK) the 32-byte password the host sent.
void log_security_event(const char *event, uint8_t opcode, ide_registers_t *regs,
                        const uint8_t *password_data = nullptr, size_t password_len = 0);
