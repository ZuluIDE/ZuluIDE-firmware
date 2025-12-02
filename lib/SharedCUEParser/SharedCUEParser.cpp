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
char SharedCUEParser::_current_file_loaded[CUE_MAX_FULL_FILEPATH + 1] = {0};
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
    set("");
}

SharedCUEParser::SharedCUEParser(const char* path)
{
    set(path);
}

 void SharedCUEParser::set(const char* path)
 {
    strlcpy(_cue_filepath, path, sizeof(_cue_filepath));
    m_cue_sheet = _shared_cuesheet;
    if (path[0] == '\0' && _shared_cuesheet[0] == '\0')
    {
        write_default_cuesheet(_shared_cuesheet);
    }
    restart();
 }

// Restart parsing from beginning of file
void SharedCUEParser::restart()
{
    update_file();
    CUEParser::restart();
}
const CUETrackInfo *SharedCUEParser::next_track()
{ 
    update_file();
    return CUEParser::next_track();
}


const CUETrackInfo *SharedCUEParser::next_track(uint64_t prev_file_size)
{
    update_file();
    return CUEParser::next_track(prev_file_size);
}

void SharedCUEParser::update_file()
{
    if ( strcasecmp(_cue_filepath, _current_file_loaded) != 0)
    {
        strlcpy(_current_file_loaded, _cue_filepath, sizeof(_current_file_loaded));
        // Empty filepath, load simple cuesheet
        if (_current_file_loaded[0] == '\0')
        {
            write_default_cuesheet(_shared_cuesheet);
        }
        else
        {

            FsFile file = SD.open(_current_file_loaded);
            if (file.isOpen())
            {
                int count = file.read(_shared_cuesheet, sizeof(_shared_cuesheet));
                file.close();
                // on read error, set _shared_cuesheet to an empty string;
                if (count <= 0)
                {
                    _shared_cuesheet[0] = '\0';
                }
                else
                {
                    // Null terminate data into a valid string
                    _shared_cuesheet[count] = '\0';
                }
            }
            else
            {
                // on open error, set _shared_cuesheet to an empty string;
                _shared_cuesheet[0] = '\0';
            }
        }
    }
}

