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
    m_blockdev(nullptr), m_contiguous(false), m_first_sector(0), m_capacity(0),
    m_read_only(false), m_buffer(buffer), m_buffer_size(buffer_size), m_drive_type(DRIVE_TYPE_VIA_PREFIX), m_lone_image(false)
{
    memset(m_prefix, 0, sizeof(m_prefix));
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

static bool is_valid_filename(const char *name)
{
    if (strcasecmp(name, "ice5lp1k_top_bitmap.bin") == 0)
    {
        // Ignore FPGA bitstream
        return false;
    }

    if (!isalnum(name[0]))
    {
        // Skip names beginning with special character
        return false;
    }

    if (name[0] && tolower(name[0] == 'z') &&
        name[1] && tolower(name[1] == 'u') &&
        name[2] && tolower(name[2] == 'l') &&
        name[3] && tolower(name[3] == 'u')
    )
    {
        // Ignore all files that start with "zulu"
        return false;
    }

    // Check file extension
    const char *extension = strrchr(name, '.');
    if (extension)
    {
        const char *ignore_exts[] = {
            ".cue", ".txt", ".rtf", ".md", ".nfo", ".pdf", ".doc",
            NULL
        };
        const char *archive_exts[] = {
            ".tar", ".tgz", ".gz", ".bz2", ".tbz2", ".xz", ".zst", ".z",
            ".zip", ".zipx", ".rar", ".lzh", ".lha", ".lzo", ".lz4", ".arj",
            ".dmg", ".hqx", ".cpt", ".7z", ".s7z",
            NULL
        };

        for (int i = 0; ignore_exts[i]; i++)
        {
            if (strcasecmp(extension, ignore_exts[i]) == 0)
            {
                // ignore these without log message
                return false;
            }
        }
        for (int i = 0; archive_exts[i]; i++)
        {
            if (strcasecmp(extension, archive_exts[i]) == 0)
            {
                logmsg("-- Ignoring compressed file ", name);
                return false;
            }
        }
    }

    return true;
}

bool IDEImageFile::load_next_image()
{
    char prev_image[MAX_FILE_PATH];
    char image_file[MAX_FILE_PATH];
    
    if (get_filename(prev_image, MAX_FILE_PATH))
    {
        close();
        if (find_next_image("/", prev_image, image_file, MAX_FILE_PATH))
        {
            open_file(SD.vol(), image_file);
            return true;
        }
    }
    return false;
}
// Find the next image file in alphabetical order.
// If prev_image is NULL, returns the first image file.
bool IDEImageFile::find_next_image(const char *directory, const char *prev_image, char *result, size_t buflen, bool lone_image)
{
    FsFile root;
    FsFile file;
    bool first_search = prev_image == NULL;
    if (lone_image && !m_lone_image )
    {
        m_lone_image = true;
    }

    if (!root.open(directory))
    {
        logmsg("Could not open directory: ", directory);
        return false;
    }

    result[0] = '\0';

    while (file.openNext(&root, O_RDONLY))
    {
        if (file.isDirectory()) continue;

        char candidate[MAX_FILE_PATH];
        file.getName(candidate, sizeof(candidate));
        const char *extension = strrchr(candidate, '.');

        if (!is_valid_filename(candidate))
        {
            continue;
        }

        if (m_lone_image)
        {
            // Assuming there has been an exhaustive search
            // find the lone image ignoring a prefix and ignoring any extension
            file.getName(result, buflen);
            break;     
        }

        if (get_drive_type() != DRIVE_TYPE_VIA_PREFIX || !get_prefix()[0])
        {
            // device type not defined by prefix
            if (extension)
            {
                if ((strcasecmp(extension, ".iso") == 0 ||
                    strcasecmp(extension, ".bin") == 0))
                {
                    if(get_drive_type() != DRIVE_TYPE_CDROM)
                    {
                        // device type does not match extention
                        continue;
                    }
                }
                else if (strcasecmp(extension, ".img") != 0)
                {
                    // device type does not match extention
                    continue;
                }
            }
        }
        if (first_search && get_drive_type() == DRIVE_TYPE_VIA_PREFIX)
        {
            bool valid_imagefile = true;
            if (strlen(candidate) >= 4)
            {
                char prefix[5] = {0};
                find_prefix(prefix, candidate);
                if (strcasecmp(prefix, "cdrm") == 0)
                {
                    set_prefix(prefix);
                    set_drive_type(DRIVE_TYPE_CDROM);
                }
                else if (strcasecmp(prefix, "zipd") == 0)
                {
                    set_prefix(prefix);
                    set_drive_type(DRIVE_TYPE_ZIP100);
                }
                else if (strcasecmp(prefix, "remv") == 0)
                {
                    set_prefix(prefix);
                    set_drive_type(DRIVE_TYPE_REMOVABLE);
                }
                else
                {
                    valid_imagefile = false;
                }
            }
            else
            {
                valid_imagefile = false;
            }
            
            if (!valid_imagefile)
            {
                continue;
            }
        }

        char prefix[5] = {0};
        find_prefix(prefix, candidate);
        if (!first_search && get_prefix()[0] && strcasecmp(get_prefix(), prefix) != 0)
        {
            // look for only image with the same prefix
            continue;
        }
        if (prev_image && strcasecmp(candidate, prev_image) <= 0)
        {
            // Alphabetically before the previous image
            continue;
        }

        if (result[0] && strcasecmp(candidate, result) >= 0)
        {
            // Alphabetically later than current result
            continue;
        }

        // Copy as the best result
        file.getName(result, buflen);
    }
    file.close();
    root.close();
    if (result[0] == '\0' )
    {
        if (first_search)
        {
            static bool lone_file_once = false;
            if (lone_file_once == false)
            {
                lone_file_once = true;
                // check for a lone file without extension restrictions or prefixes
                find_next_image(directory, NULL, result, buflen, true);
            }
        }
        else
        {
            // wrap search
            find_next_image(directory, NULL, result, buflen);
        }
    }

    return result[0] != '\0';
}


void IDEImageFile::set_drive_type(drive_type_t type)
{
    m_drive_type = type;
}


drive_type_t IDEImageFile::get_drive_type()
{
    return m_drive_type;
}

void IDEImageFile::set_prefix(char *prefix)
{
    strcpy(m_prefix, prefix);
}

const char* const IDEImageFile::get_prefix()
{
    return m_prefix;
}

void IDEImageFile::find_prefix(char *prefix, const char* file_name)
{
    for (int i=0; i < 4; i++)
    {
        prefix[i] = tolower(file_name[i]);
    }
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
        if (sd_cb_state.blocks_done < sd_cb_state.blocks_available)
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
                sd_cb_state.blocks_done += max_write;
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
            uint8_t *data_start = sd_cb_state.buffer + start_idx * sd_cb_state.blocksize;
            ssize_t status = sd_cb_state.callback->write_callback(data_start, sd_cb_state.blocksize, max_read);
            if (status < 0)
                sd_cb_state.error = true;
            else
                sd_cb_state.blocks_available += status;
        }
    }
}