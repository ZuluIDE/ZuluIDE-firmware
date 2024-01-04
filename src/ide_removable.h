// Implements ATAPI command handlers for emulating a Zip drive

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