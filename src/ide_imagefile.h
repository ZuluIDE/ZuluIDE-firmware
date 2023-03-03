#pragma once
#include <stddef.h>
#include <SdFat.h>

// Interface for emulated image files
class IDEImage
{
public:
    // Callback interface for use with read() and write()
    class Callback
    {
    public:
        // Callback for data that has been read from image file.
        // data:    Pointer to data that has been read from SD card
        // bytes:   Number of bytes available
        // returns: Number of bytes processed (buffer can be reused) or negative on error
        virtual ssize_t read_callback(const uint8_t *data, size_t bytes) = 0;

        // Callback for getting data for writing to image file.
        // data:    Pointer where data should be written
        // bytes:   Number of bytes that can be written to 'data'
        // returns: Number of bytes available at 'data' or negative on error
        virtual ssize_t write_callback(uint8_t *data, size_t bytes) = 0;
    };

    // Return image size in bytes
    virtual uint64_t capacity();
    
    // Is the image file writable?
    virtual bool writable();

    // Read data from image file using a callback interface.
    // The callback function is passed a data pointer and number of bytes available.
    // It will return the number of bytes it has processed - any unprocessed bytes
    // will be given again on next call. Once bytes have been processed the buffer
    // may be reused.
    // Typical usage is that first callback starts a transfer and returns 0, and
    // a later callback will return the count once the transfer has completed.
    virtual bool read(uint64_t startpos, size_t num_bytes, Callback *callback) = 0;

    // Write data to image file using a callback interface.
    // The callback function is passed data pointer and number of bytes requested.
    // It will return the number of bytes available at data.
    // Typical usage is that first callback starts a transfer and returns 0, and
    // a later callback will return the count once the transfer has completed.
    virtual bool write(uint64_t startpos, size_t num_bytes, Callback *callback) = 0;
};

// Implementation for SD-card based image files
class IDEImageFile: public IDEImage
{
public:
    IDEImageFile();
    IDEImageFile(uint8_t *buffer, size_t buffer_size);

    bool open_file(FsVolume *volume, const char *filename, bool read_only = false);

    virtual uint64_t capacity();
    virtual bool writable();
    virtual bool read(uint64_t startpos, size_t num_bytes, IDEImage::Callback *callback);
    virtual bool write(uint64_t startpos, size_t num_bytes, IDEImage::Callback *callback);

protected:
    FsFile m_file;
    SdCard *m_blockdev;

    bool m_contiguous;
    uint32_t m_first_sector;

    uint64_t m_capacity;
    bool m_read_only;
    uint8_t *m_buffer;
    size_t m_buffer_size;
    size_t m_buffer_size_mask;
};
