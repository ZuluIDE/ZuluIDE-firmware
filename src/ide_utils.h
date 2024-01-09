/**
 * ZuluIDE™ - Copyright (c) 2023 Rabbit Hole Computing™
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details. 
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
**/

// Utilities for IDE and ATAPI command implementations

#pragma once

#include <stdint.h>

// Utilities for parsing and writing command arguments (big-endian)
uint16_t parse_be16(const uint8_t *src);
uint32_t parse_be24(const uint8_t *src);
uint32_t parse_be32(const uint8_t *src);
void write_be16(uint8_t *dst, uint16_t value);
void write_be24(uint8_t *dst, uint32_t value);
void write_be32(uint8_t *dst, uint32_t value);