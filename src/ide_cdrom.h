// Implements ATAPI command handlers for emulating a CD-ROM drive.

#pragma once

#include "ide_atapi.h"
#include <CUEParser.h>

class IDECDROMDevice: public IDEATAPIDevice
{
public:
    IDECDROMDevice();

    virtual void set_image(IDEImage *image);

protected:
    
    virtual bool handle_atapi_command(const uint8_t *cmd);
    virtual bool atapi_read_disc_information(const uint8_t *cmd);
    virtual bool atapi_read_toc(const uint8_t *cmd);

    bool doReadTOC(bool MSF, uint8_t track, uint16_t allocationLength);
    bool doReadSessionInfo(bool MSF, uint16_t allocationLength);
    bool doReadFullTOC(uint8_t session, uint16_t allocationLength, bool useBCD);

    // Access data from CUE sheet, or dummy data if no cue sheet provided
    char m_cuesheet[1024];
    CUEParser m_cueparser;
    bool loadAndValidateCueSheet(const char *cuesheetname);
    bool getFirstLastTrackInfo(CUETrackInfo &first, CUETrackInfo &last);
    uint32_t getLeadOutLBA(const CUETrackInfo* lasttrack);
};
