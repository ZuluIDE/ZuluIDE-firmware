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