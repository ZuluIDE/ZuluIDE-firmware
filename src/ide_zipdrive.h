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

// Implements ATAPI command handlers for emulating a Zip drive

#pragma once

#include "ide_atapi.h"

enum class zip_drive_type_t {Zip100, Zip250, Zip750};

class IDEZipDrive: public IDEATAPIDevice
{
public:
    virtual void initialize(int devidx);

    virtual void set_image(IDEImage *image);

    virtual uint64_t capacity() override;

    virtual bool handle_command(ide_registers_t *regs) override;

    virtual bool disables_iordy() override { return true; }

    virtual void eject_media() override;

    virtual void button_eject_media() override;

    virtual void insert_media(IDEImage *image = nullptr) override;

    virtual bool set_load_deferred(const char* image_name) override;
    virtual bool is_load_deferred() override;

protected:
    virtual bool cmd_identify_packet_device(ide_registers_t *regs) override;
    virtual bool cmd_get_media_status(ide_registers_t *regs);
    virtual bool cmd_set_features(ide_registers_t *regs) override;
    virtual bool handle_atapi_command(const uint8_t *cmd) override;

    virtual bool atapi_format_unit(const uint8_t *cmd);
    virtual bool atapi_read_format_capacities(const uint8_t *cmd);
    virtual bool atapi_verify(const uint8_t *cmd);
    virtual bool atapi_inquiry(const uint8_t *cmd) override;
    virtual bool atapi_start_stop_unit(const uint8_t *cmd) override;
    virtual bool atapi_zip_disk_0x06(const uint8_t *cmd);
    virtual bool atapi_zip_disk_0x0D(const uint8_t *cmd);


    virtual size_t atapi_get_mode_page(uint8_t page_ctrl, uint8_t page_idx, uint8_t *buffer, size_t max_bytes) override;
    bool m_media_status_notification;

    struct
    {
        bool button_pressed;
        char serial_string[27];
    } m_zip_disk_info;
};
