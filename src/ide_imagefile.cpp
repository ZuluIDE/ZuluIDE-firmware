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

#include "ide_imagefile.h"
#include <strings.h>
#include "ZuluIDE.h"
#include "ZuluIDE_config.h"
#include <assert.h>
#include <algorithm>

// SD card callbacks from platform code use global state
IDEImageFile::sd_cb_state_t IDEImageFile::sd_cb_state;

IDEImageFile::IDEImageFile(): IDEImageFile(nullptr, 0)
{

}

IDEImageFile::IDEImageFile(uint8_t *buffer, size_t buffer_size):
    m_buffer(buffer), m_buffer_size(buffer_size), m_drive_type(DRIVE_TYPE_VIA_PREFIX)
{
    clear();
    memset(m_prefix, 0, sizeof(m_prefix));
}

void IDEImageFile::clear()
{
    m_blockdev = nullptr;
    m_contiguous = false;
    m_first_sector = 0;
    m_capacity = 0;
    m_read_only = false;

}

bool IDEImageFile::open_file(const char *filename, bool read_only)
{
  return open_file(SD.vol(), filename, read_only);
}

bool IDEImageFile::open_file(FsVolume *volume, const char *filename, bool read_only)
{
    if (volume->attrib(filename) & FS_ATTRIB_READ_ONLY)
    {
        read_only = true;
    }

    m_contiguous = false;
    m_read_only = read_only;
    m_file.close();
    m_file = volume->open(filename, read_only ? O_RDONLY : O_RDWR);

    if (!m_file.isOpen())
    {
        m_capacity = 0;
        return false;
    }

    m_capacity = m_file.size();

    uint32_t begin = 0, end = 0;
    if (m_file.contiguousRange(&begin, &end))
    {
        dbgmsg("Image file ", filename, " is contiguous, sectors ", (int)begin, " to ", (int)end);
        m_first_sector = begin;
        m_contiguous = true;
    }
    else
    {
        logmsg("Image file ", filename, " is not contiguous, access will be slower");
    }

    return true;
}

void IDEImageFile::close()
{
    m_file.close();
}

bool IDEImageFile::get_filename(char *buf, size_t buflen)
{
    if (!m_file.isOpen())
    {
        buf[0] = '\0';
        return false;
    }
    else
    {
        m_file.getName(buf, buflen);
        return true;
    }
}

void IDEImageFile::set_drive_type(drive_type_t type)
{
    m_drive_type = type;
}


drive_type_t IDEImageFile::get_drive_type()
{
    return m_drive_type;
}

void IDEImageFile::set_prefix(const char *prefix)
{
    strcpy(m_prefix, prefix);
}

const char* const IDEImageFile::get_prefix()
{
    return m_prefix;
}


uint64_t IDEImageFile::capacity()
{
    return m_capacity;
}

uint64_t IDEImageFile::file_position()
{
    return m_file.position();
}

bool IDEImageFile::is_open()
{
    return m_file.isOpen();
}

bool IDEImageFile::writable()
{
    return !m_read_only;
}

/******************************/
/* Data transfer from SD card */
/******************************/

bool IDEImageFile::read(uint64_t startpos, size_t blocksize, size_t num_blocks, Callback *callback)
{
    if (!m_file.seek(startpos)) return false;

    assert(blocksize <= m_buffer_size);

    sd_cb_state.callback = callback;
    sd_cb_state.error = false;
    sd_cb_state.buffer = m_buffer;
    sd_cb_state.num_blocks = num_blocks;
    sd_cb_state.blocksize = blocksize;
    sd_cb_state.blocks_done = 0;
    sd_cb_state.blocks_available = 0;
    sd_cb_state.bufsize_blocks = m_buffer_size / blocksize;

    while (sd_cb_state.blocks_done < num_blocks && !sd_cb_state.error)
    {
        platform_poll();

        // Check if we have buffer space to read more from SD card
        if (sd_cb_state.blocks_available < num_blocks &&
            sd_cb_state.blocks_available < sd_cb_state.blocks_done + sd_cb_state.bufsize_blocks)
        {
            // Check how many contiguous blocks we have space available for.
            // Limit by:
            // 1. Total requested transfer size
            // 2. Number of free slots in buffer
            // 3. Space until wrap point of the buffer
            size_t start_idx = sd_cb_state.blocks_available % sd_cb_state.bufsize_blocks;
            size_t max_read = std::min({
                num_blocks - sd_cb_state.blocks_available,
                sd_cb_state.blocks_done + sd_cb_state.bufsize_blocks - sd_cb_state.blocks_available,
                sd_cb_state.bufsize_blocks - start_idx
            });

            // Read from SD card and process callbacks
            uint8_t *buf = m_buffer + blocksize * start_idx;
            platform_set_sd_callback(&IDEImageFile::sd_read_callback, buf);
            int status = m_file.read(buf, blocksize * max_read);
            platform_set_sd_callback(nullptr, nullptr);

            // Check status of SD card read
            if (status != blocksize * max_read)
                sd_cb_state.error = true;
            else
                sd_cb_state.blocks_available += max_read;
        }

        // Provide callbacks until all blocks have been processed,
        // even if SD card read is done.
        if (sd_cb_state.blocks_done < sd_cb_state.blocks_available)
        {
            sd_read_callback(0);
        }
    }

    return !sd_cb_state.error;
}

void IDEImageFile::sd_read_callback(uint32_t bytes_complete)
{
    // Update number of blocks available by the latest callback status.
    // sd_cb_state.blocks_available will be updated when SD card read() returns.
    size_t blocks_available = sd_cb_state.blocks_available + bytes_complete / sd_cb_state.blocksize;

    // Check how many contiguous blocks are available to process.
    size_t start_idx = sd_cb_state.blocks_done % sd_cb_state.bufsize_blocks;
    size_t max_write = std::min({
        blocks_available - sd_cb_state.blocks_done,
        sd_cb_state.bufsize_blocks - start_idx
    });

    if (max_write > 0)
    {
        uint8_t *data_start = sd_cb_state.buffer + start_idx * sd_cb_state.blocksize;
        ssize_t status = sd_cb_state.callback->read_callback(data_start, sd_cb_state.blocksize, max_write);
        if (status < 0)
            sd_cb_state.error = true;
        else
            sd_cb_state.blocks_done += status;
    }
}

/******************************/
/* Data transfer to SD card */
/******************************/

// For now this uses simple blocking access, because we don't need CD-ROM write yet.
bool IDEImageFile::write(uint64_t startpos, size_t blocksize, size_t num_blocks, Callback *callback)
{
    if (!m_file.seek(startpos)) return false;

    assert(blocksize <= m_buffer_size);

    sd_cb_state.callback = callback;
    sd_cb_state.error = false;
    sd_cb_state.buffer = m_buffer;
    sd_cb_state.num_blocks = num_blocks;
    sd_cb_state.blocksize = blocksize;
    sd_cb_state.blocks_done = 0;
    sd_cb_state.blocks_available = 0;
    sd_cb_state.bufsize_blocks = m_buffer_size / blocksize;

    while (sd_cb_state.blocks_done < num_blocks && !sd_cb_state.error)
    {
        platform_poll();

        // Check if callback can provide more data
        sd_write_callback(0);

        // Check if there is data to be written to SD card
        if (sd_cb_state.blocks_done < sd_cb_state.num_blocks)
        {
            // Check how many contiguous blocks are available to process.
            size_t start_idx = sd_cb_state.blocks_done % sd_cb_state.bufsize_blocks;
            size_t max_write = std::min({
                sd_cb_state.blocks_available - sd_cb_state.blocks_done,
                sd_cb_state.bufsize_blocks - start_idx
            });

            // Write data to SD card and process callbacks
            uint8_t *buf = m_buffer + blocksize * start_idx;
            platform_set_sd_callback(&IDEImageFile::sd_write_callback, buf);
            int status = m_file.write(buf, blocksize * max_write);
            platform_set_sd_callback(nullptr, nullptr);

            // Check status of SD card write
            if (status != blocksize * max_write)
                sd_cb_state.error = true;
            else
            {
                sd_cb_state.blocks_done += max_write;
            }
        }
    }

    return !sd_cb_state.error;
}

void IDEImageFile::sd_write_callback(uint32_t bytes_complete)
{
    // Update number of blocks done by the latest callback status.
    // sd_cb_state.blocks_done will be updated when SD card write() returns.
    size_t blocks_done = sd_cb_state.blocks_done + bytes_complete / sd_cb_state.blocksize;

    if (sd_cb_state.blocks_available < sd_cb_state.num_blocks &&
        sd_cb_state.blocks_available < blocks_done + sd_cb_state.bufsize_blocks)
    {
        // Check how many contiguous blocks we have space available for.
        // Limit by:
        // 1. Total requested transfer size
        // 2. Number of free slots in buffer
        // 3. Space until wrap point of the buffer
        size_t start_idx = sd_cb_state.blocks_available % sd_cb_state.bufsize_blocks;
        size_t max_read = std::min({
            sd_cb_state.num_blocks - sd_cb_state.blocks_available,
            sd_cb_state.blocks_done + sd_cb_state.bufsize_blocks - sd_cb_state.blocks_available,
            sd_cb_state.bufsize_blocks - start_idx
        });

        if (max_read > 0)
        {
            // Receive data from callback
            bool last_xfer = sd_cb_state.num_blocks == sd_cb_state.blocks_done + max_read;
            bool first_xfer = sd_cb_state.blocks_done == 0;
            uint8_t *data_start = sd_cb_state.buffer + start_idx * sd_cb_state.blocksize;
            ssize_t status = sd_cb_state.callback->write_callback(data_start, sd_cb_state.blocksize, max_read, first_xfer, last_xfer);
            if (status < 0)
                sd_cb_state.error = true;
            else
                sd_cb_state.blocks_available += status;
        }
    }
}
