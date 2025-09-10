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
 * Under Section 7 of GPL version 3, you are granted additional
 * permissions described in the ZuluIDE Hardware Support Library Exception
 * (GPL-3.0_HSL_Exception.md), as published by Rabbit Hole Computing™.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
**/

#include "ide_utils.h"

uint16_t parse_be16(const uint8_t *src)
{
    return ((uint16_t)src[0] << 8) | src[1];
}

uint32_t parse_be24(const uint8_t *src)
{
    return ((uint32_t)src[0] << 16) |
           ((uint32_t)src[1] << 8) |
           ((uint32_t)src[2]);
}

uint32_t parse_be32(const uint8_t *src)
{
    return ((uint32_t)src[0] << 24) |
           ((uint32_t)src[1] << 16) |
           ((uint32_t)src[2] << 8) |
           ((uint32_t)src[3]);
}

void write_be16(uint8_t *dst, uint16_t value)
{
    dst[0] = (value >> 8) & 0xFF;
    dst[1] = (value) & 0xFF;
}

void write_be24(uint8_t *dst, uint32_t value)
{
    dst[0] = (value >> 16) & 0xFF;
    dst[1] = (value >> 8) & 0xFF;
    dst[2] = (value) & 0xFF;
}

void write_be32(uint8_t *dst, uint32_t value)
{
    dst[0] = (value >> 24) & 0xFF;
    dst[1] = (value >> 16) & 0xFF;
    dst[2] = (value >> 8) & 0xFF;
    dst[3] = (value) & 0xFF;
}