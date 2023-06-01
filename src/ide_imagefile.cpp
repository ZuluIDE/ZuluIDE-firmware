#include "ide_imagefile.h"
#include "ZuluIDE.h"
#include <assert.h>

#define SD_BLOCKSIZE 512

IDEImageFile::IDEImageFile(): IDEImageFile(nullptr, 0)
{

}

IDEImageFile::IDEImageFile(uint8_t *buffer, size_t buffer_size):
    m_blockdev(nullptr), m_contiguous(false), m_first_sector(0), m_capacity(0),
    m_read_only(false), m_buffer(buffer), m_buffer_size(buffer_size)
{
    // Buffer size must be power of 2
    m_buffer_size_mask = (m_buffer_size - 1);
    assert((m_buffer_size & m_buffer_size_mask) == 0);
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

uint64_t IDEImageFile::capacity()
{
    return m_capacity;
}

bool IDEImageFile::writable()
{
    return !m_read_only;
}

/******************************/
/* Data transfer from SD card */
/******************************/

// TODO: optimize with SD callbacks
bool IDEImageFile::read(uint64_t startpos, size_t num_bytes, IDEImage::Callback *callback)
{
    if (!m_file.seek(startpos)) return false;

    size_t bytes_done = 0;
    size_t bytes_available = 0;
    while (bytes_done < num_bytes)
    {
        platform_poll();

        // Check if we have buffer space to read more from SD card
        if (bytes_available < num_bytes &&
            bytes_available < bytes_done + m_buffer_size)
        {
            size_t start_idx = bytes_available & m_buffer_size_mask;
            size_t max_read = num_bytes - bytes_available;
            if (start_idx + max_read > m_buffer_size) max_read = m_buffer_size - start_idx;

            if (m_file.read(m_buffer + start_idx, max_read) != max_read)
            {
                return false;
            }

            bytes_available += max_read;
        }

        // Pass data to callback
        if (bytes_done <= bytes_available)
        {
            size_t start_idx = bytes_done & m_buffer_size_mask;
            size_t max_write = bytes_available - bytes_done;
            if (start_idx + max_write > m_buffer_size) max_write = m_buffer_size - start_idx;
            
            ssize_t status = callback->read_callback(m_buffer + start_idx, max_write);
            if (status < 0) return false;
            bytes_done += status;
        }
    }

    return true;
}

/******************************/
/* Data transfer to SD card */
/******************************/

// For now this uses simple blocking access, because we don't need CD-ROM write yet.
bool IDEImageFile::write(uint64_t startpos, size_t num_bytes, IDEImage::Callback *callback)
{
    if (!m_file.seek(startpos)) return false;

    size_t bytes_done = 0;
    size_t bytes_available = 0;
    while (bytes_done < num_bytes)
    {
        // Check if callback can provide more data
        if (bytes_available < num_bytes &&
            bytes_available < bytes_done + m_buffer_size)
        {
            size_t start_idx = bytes_available & m_buffer_size_mask;
            size_t max_read = num_bytes - bytes_available;
            
            // Limit to buffer wrap point
            if (start_idx + max_read > m_buffer_size)
            {
                max_read = m_buffer_size - start_idx;
            }
            
            // Receive data from callback
            ssize_t status = callback->write_callback(m_buffer + start_idx, max_read);
            if (status < 0) return false;
            bytes_available += status;
        }

        // Check if we have received a complete block
        if (bytes_available >= bytes_done + SD_BLOCKSIZE || bytes_available == num_bytes)
        {
            // Complete block available, write it to SD card
            size_t start_idx = bytes_done & m_buffer_size_mask;
            size_t max_write = bytes_available - bytes_done;
            if (start_idx + max_write > m_buffer_size) max_write = m_buffer_size - start_idx;
            if (m_file.write(m_buffer + start_idx, max_write) != max_write)
            {
                return false;
            }
            bytes_done += max_write;
        }
    }

    return true;
}