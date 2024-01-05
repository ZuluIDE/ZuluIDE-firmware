/**
 * ZuluIDE™ - Copyright (c) 2024 Rabbit Hole Computing™
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

// Implements ATAPI command handlers for emulating a generic removable drive

#pragma once

#include "ide_atapi.h"

class IDERemovable: public IDEATAPIDevice
{
public:
    virtual void initialize(int devidx);

    virtual void set_image(IDEImage *image);

    virtual uint64_t capacity() override;

protected:
    virtual bool handle_atapi_command(const uint8_t *cmd) override;

    virtual bool atapi_format_unit(const uint8_t *cmd);
    virtual bool atapi_read_format_capacities(const uint8_t *cmd);
    virtual bool atapi_verify(const uint8_t *cmd);

    virtual size_t atapi_get_mode_page(uint8_t page_ctrl, uint8_t page_idx, uint8_t *buffer, size_t max_bytes) override;
};