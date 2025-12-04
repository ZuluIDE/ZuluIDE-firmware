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

#include "scp/SharedCUEParser.h"
#include <string.h>
#include <SdFat.h>

extern SdFs SD;

FsFile * SharedCUEParser::_current_cue_file = nullptr;
char SharedCUEParser::_shared_cuesheet[MAX_SHARED_CUE_SHEET_SIZE];

static void write_default_cuesheet(char * cue_sheet)
{
    strcpy(cue_sheet, R"(
    FILE "" BINARY
    TRACK 01 MODE1/2048
    INDEX 01 00:00:00
    )");
}

SharedCUEParser::SharedCUEParser()
{
    m_cue_sheet = _shared_cuesheet;
    switch_cue();
}

SharedCUEParser::SharedCUEParser(const char* path)
{
    m_cue_sheet = _shared_cuesheet;
    _cue_file.open(path);
    switch_cue();
}

FsFile *SharedCUEParser::get_cue_file()
{
    return &_cue_file;
}

// Restart parsing from beginning of file
void SharedCUEParser::restart()
{
    switch_cue();
    CUEParser::restart();
}
const CUETrackInfo *SharedCUEParser::next_track()
{ 

    switch_cue();
    return CUEParser::next_track();
}


const CUETrackInfo *SharedCUEParser::next_track(uint64_t prev_file_size)
{
    switch_cue();
    return CUEParser::next_track(prev_file_size);
}

void SharedCUEParser::load_updated_cue()
{
    load_cue();
    CUEParser::restart();
}

void SharedCUEParser::load_cue()
{
    if (!_cue_file.isOpen())
    {
        write_default_cuesheet(_shared_cuesheet);
    }
    else
    {
        _cue_file.rewind();
        int count = _cue_file.read(_shared_cuesheet, sizeof(_shared_cuesheet) - 1);
        char filename[257];
        _cue_file.getName(filename, sizeof(filename));
        // on read error, close _cue_file;
        if (count <= 0)
        {
            _cue_file.close();
        }
        else
        {
            // Null terminate data into a valid string
            _shared_cuesheet[count] = '\0';
        }
    }
}

void SharedCUEParser::switch_cue()
{
    if ( _current_cue_file != &_cue_file)
    {
        _current_cue_file = &_cue_file;
        load_cue();
    }
}

