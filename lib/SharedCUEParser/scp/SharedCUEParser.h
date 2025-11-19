#pragma once
#include <memory>
#include <CUEParser.h>
#ifndef MAX_SHARED_CUE_SHEET_SIZE 
#  define MAX_SHARED_CUE_SHEET_SIZE (12 * 1024)
#endif

class SharedCUEParser : public CUEParser
{
public:
    SharedCUEParser();
    SharedCUEParser(char* path);
    
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

    inline static size_t max_cue_sheet_size(){ return MAX_SHARED_CUE_SHEET_SIZE;}
protected:
    // Checks to see if the current buffer is using the needed cue file
    // If not, loads the correct _cue_file into the _shared_cuesheet buffer
    virtual void update_file();
    char static _shared_cuesheet[MAX_SHARED_CUE_SHEET_SIZE];
    char static _current_file_loaded[CUE_MAX_FILENAME + 1];
    char _cue_filepath[CUE_MAX_FILENAME + 1];

};