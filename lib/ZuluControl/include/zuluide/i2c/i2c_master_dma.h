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

#pragma once

#include <hardware/i2c.h>
#include <pico/time.h>
#include <cstdint>
#include <cstddef>
#include <ZuluControl_platform.h>
// DMA-driven I2C master transmit path used specifically for sending
// ZuluControl-firmware upgrade chunks (I2C_SERVER_UPDATE_FW_DATA). The host's transmit
// side (mirrors arduino-pico's own TwoWire::writeReadAsync).
// Everything else in the upgrade protocol control messages (START/FINISH/ABORT/RETRY)
// and the client's ACK uses the existing Wire-based writeLengthPrefacedString / I2CServer::Poll()
// mechanism, same as every other message in this protocol.
//
// Wire protocol (big-endian throughout):
//   Chunk-data message: [cmd][len_hi][len_lo][payload: up to 2048 bytes]
//   Ack message:         [cmd][len_hi][len_lo][crc32: 4 bytes]
//
// CRC32 convention (must match the client's software CRC32 exactly): the
// same reflected CRC-32/ISO-HDLC algorithm table-driven, polynomial
// 0xEDB88320 (the bit-reversed form of 0x04C11DB7), seed
// I2C_UPGRADE_CRC32_SEED, result XORed with I2C_UPGRADE_CRC32_FINAL_XOR.
// Computed in software on both ends via an identical routine.
#define I2C_UPGRADE_CRC32_SEED 0xFFFFFFFFu
#define I2C_UPGRADE_CRC32_FINAL_XOR 0xFFFFFFFFu

namespace zuluide::i2c {

// Must be called once, passing the same i2c_inst_t already used by the shared
// Wire instance (e.g. GPIO_I2C_DEVICE), before any of the functions below are used.
bool I2CMasterDmaInit(i2c_inst_t* i2c);

// Sends [cmd][len_hi][len_lo][payload...] to `addr` as a header-DMA-channel
// chained to a data-DMA-channel (the actual wire transfer), while separately
// computing the CRC32 of `payload` in software. Blocks until the transfer
// completes or `until` is reached. Returns false on any I2C-level abort
// (NACK/timeout); `*out_sent_crc` is only valid when this returns true. The
// client independently computes its own CRC32 of what it actually received
// and reports it in the ACK -- the caller is responsible for comparing that
// against `*out_sent_crc` to decide whether to retry.
bool I2CMasterDmaSendChunk(uint8_t addr, uint8_t cmd, const uint8_t* payload,
                           uint16_t length, uint32_t* out_sent_crc,
                           absolute_time_t until);

}  // namespace zuluide::i2c
