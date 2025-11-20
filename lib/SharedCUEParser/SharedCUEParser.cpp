    
#include "scp/SharedCUEParser.h"
#include <string.h>
#include <SdFat.h>

extern SdFs SD;
char SharedCUEParser::_current_file_loaded[CUE_MAX_FILENAME + 1] = {0};
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
    _cue_filepath[0] = '\0';
    m_cue_sheet = _shared_cuesheet;
    if (_shared_cuesheet[0] == '\0')
    {
        write_default_cuesheet(_shared_cuesheet);
    }
    restart();
}

SharedCUEParser::SharedCUEParser(char* path)
{
    strcpy(_cue_filepath, path);
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
        strcpy(_current_file_loaded, _cue_filepath);
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

