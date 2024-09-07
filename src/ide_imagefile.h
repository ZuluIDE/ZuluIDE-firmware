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

#pragma once
#include <stddef.h>
#include <SdFat.h>
#include <zuluide/ide_drive_type.h>

// Interface for emulated image files
class IDEImage
{
public:
    // Callback interface for use with read() and write()
    class Callback
    {
    public:
        // Callback for data that has been read from image file.
        // data:        Pointer to data that has been read from SD card
        // blocksize:   Block size passed to read() call
        // num_blocks:  Number of blocks available at data pointer
        // returns:     Number of blocks processed (buffer can be reused) or negative on error
        virtual ssize_t read_callback(const uint8_t *data, size_t blocksize, size_t num_blocks) = 0;

        // Callback for getting data for writing to image file.
        // data:        Pointer where data should be written
        // blocksize:   Block size passed to read() call
        // num_blocks:  Maximum number of blocks that can be written to data.
        // returns:     Number of blocks written to 'data' or negative on error
        virtual ssize_t write_callback(uint8_t *data, size_t blocksize, size_t num_blocks, bool first_xfer, bool last_xfer) = 0;
    };

    // Return filename or false if not file-backed
    virtual bool get_filename(char *buf, size_t buflen) = 0;

    // Return image size in bytes
    virtual uint64_t capacity() = 0;

    // Is the image file writable?
    virtual bool writable() = 0;

    // Read data from image file using a callback interface.
    // The callback function is passed a data pointer and number of blocks available.
    // It will return the number of blocks it has processed - any unprocessed blocks
    // will be given again on next call. Once blocks have been processed the buffer
    // may be reused.
    //
    // Transfers can be performed byte-per-byte, in which case blocksize = 1.
    // If blocksize > 1, the callback is provided integer number of blocks at a time.
    //
    // Typical usages:
    //   1. Synchronous handling of data: callback will pass forward the data, wait
    //      for it to be transferred and return the number of blocks transferred.
    //
    //   2. Asynchronous handling of data: callback will start a transfer, return 0,
    //      and on next call return the number of blocks handled.
    virtual bool read(uint64_t startpos, size_t blocksize, size_t num_blocks, Callback *callback) = 0;

    // Write data to image file using a callback interface.
    // The callback function is passed data pointer and number of blocks requested.
    // It will return the number of blocks available at data.
    virtual bool write(uint64_t startpos, size_t blocksize, size_t num_blocks, Callback *callback) = 0;

    // \todo This should really be moved to IDEDevice somehow
    virtual void set_drive_type(drive_type_t type) = 0;
    virtual drive_type_t get_drive_type() = 0;


};

// Implementation for SD-card based image files
class IDEImageFile: public IDEImage
{
public:
    IDEImageFile();
    IDEImageFile(uint8_t *buffer, size_t buffer_size);

    void clear();

    bool open_file(FsVolume *volume, const char *filename, bool read_only = false);
    bool open_file(const char* filename, bool read_only = false);
    void close();

    virtual bool get_filename(char *buf, size_t buflen);
    virtual uint64_t capacity();
    virtual uint64_t file_position();
    virtual bool is_open();
    virtual bool writable();
    virtual bool read(uint64_t startpos, size_t blocksize, size_t num_blocks, Callback *callback);
    virtual bool write(uint64_t startpos, size_t blocksize, size_t num_blocks, Callback *callback);

    // Set drive type for filtering purposes
    virtual void set_drive_type(drive_type_t type);
    virtual drive_type_t get_drive_type();

    // Set the prefix string of the filename, to match next file to insert after ejection
    virtual void set_prefix(const char* prefix);
    virtual const char* const get_prefix();

    // This is direct access to the file object, ideally this should be remove
    // But this makes importing the audio playback code easier
    virtual FsFile* direct_file() {return &m_file;}

protected:
    FsFile m_file;
    SdCard *m_blockdev;

    bool m_contiguous;
    uint32_t m_first_sector;

    uint64_t m_capacity;
    bool m_read_only;
    uint8_t *m_buffer;
    size_t m_buffer_size;

    char m_prefix[5];
    drive_type_t m_drive_type;

    struct sd_cb_state_t {
        IDEImage::Callback *callback;
        bool error;
        uint8_t *buffer;
        size_t num_blocks;
        size_t blocksize;
        size_t bufsize_blocks;
        size_t blocks_done;
        size_t blocks_available;
    };
    static sd_cb_state_t sd_cb_state;
    static void sd_read_callback(uint32_t bytes_complete);
    static void sd_write_callback(uint32_t bytes_complete);
};
