/**
 * ZuluIDE™ - Copyright (c) 2025 Rabbit Hole Computing™
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

#pragma once
#include <memory>
#include <CUEParser.h>
#ifndef MAX_SHARED_CUE_SHEET_SIZE 
#  define MAX_SHARED_CUE_SHEET_SIZE (12 * 1024)
#endif
// Something like "/path_from_root/filename"
#define CUE_MAX_FULL_FILEPATH (2 * CUE_MAX_FILENAME + 2 /* two slashes*/)

class SharedCUEParser : public CUEParser
{
public:
    SharedCUEParser();
    SharedCUEParser(const char* path);
    
    virtual void set(const char* path);
    // Restart parsing from beginning of file
    virtual void restart() override;

    // Get information for next track.
    // Returns nullptr when there are no more tracks.
    // The returned pointer remains valid until next call to next_track()
    // or destruction of this object.
    virtual const CUETrackInfo *next_track() override;

    // Same as next_track(), but takes the file size into account when
    // switching files. This is necessary for getting the correct track
    // lengths when the .cue file references multiple .bin
    virtual const CUETrackInfo *next_track(uint64_t prev_file_size) override;
    inline static bool test_path_len(size_t directory_len, size_t file_len) { return (directory_len + file_len + 2 /* two slashes in path name*/) <= MAX_SHARED_CUE_SHEET_SIZE;}

    inline static size_t max_cue_sheet_size(){ return MAX_SHARED_CUE_SHEET_SIZE;}
protected:
    // Checks to see if the current buffer is using the needed cue file
    // If not, loads the correct _cue_file into the _shared_cuesheet buffer
    virtual void update_file();
    char static _shared_cuesheet[MAX_SHARED_CUE_SHEET_SIZE];
    char static _current_file_loaded[CUE_MAX_FULL_FILEPATH + 1];
    // max chars "/cue_max_filename-path/cue_max_filename
    char _cue_filepath[CUE_MAX_FULL_FILEPATH + 1];

};