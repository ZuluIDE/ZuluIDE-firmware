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

// Implements ATAPI command handlers for emulating a CD-ROM drive.

#pragma once

#include "ide_atapi.h"
#include <CUEParser.h>

// Event Status Notification handling
class IDECDROMDevice: public IDEATAPIDevice
{
public:
    virtual void initialize(int devidx) override;

    virtual void reset() override;

    virtual void set_image(IDEImage *image);

    virtual uint64_t capacity_lba() override;
    
    virtual void eject_media() override;

    virtual void button_eject_media() override;

    virtual void insert_media(IDEImage *image = nullptr) override;
    
    // esn - event status notification
    enum class esn_event_t 
    {
        NoChange,
        MEjectRequest,
        MMediaRemoval,
        MNewMedia
    };

    enum esn_class_request_t 
    {
        OperationChange = 1,
        PowerManagement,
        ExternalRequest,
        Media,
    };

    virtual void set_esn_event(esn_event_t event);
    // \todo put back in protected
    bool doPlayAudio(uint32_t lba, uint32_t length);
protected:
    
    virtual bool handle_atapi_command(const uint8_t *cmd);
    bool atapi_cmd_not_ready_error() override;
    virtual bool atapi_set_cd_speed(const uint8_t *cmd);
    virtual bool atapi_read_disc_information(const uint8_t *cmd);
    virtual bool atapi_read_track_information(const uint8_t *cmd);
    virtual bool atapi_read_sub_channel(const uint8_t *cmd);
    virtual bool atapi_read_toc(const uint8_t *cmd);
    virtual bool atapi_read_header(const uint8_t *cmd);
    virtual bool atapi_read_cd(const uint8_t *cmd);
    virtual bool atapi_read_cd_msf(const uint8_t *cmd);
    virtual bool atapi_get_event_status_notification(const uint8_t *cmd) override;
    virtual bool atapi_start_stop_unit(const uint8_t *cmd) override;
    virtual bool atapi_play_audio_10(const uint8_t *cmd);
    virtual bool atapi_play_audio_12(const uint8_t *cmd);
    virtual bool atapi_play_audio_msf(const uint8_t *cmd);
    virtual bool atapi_stop_play_scan_audio(const uint8_t *cmd);
    virtual bool atapi_pause_resume_audio(const uint8_t *cmd);
    
    bool doReadTOC(bool MSF, uint8_t track, uint16_t allocationLength);
    bool doReadSessionInfo(bool MSF, uint16_t allocationLength);
    bool doReadFullTOC(uint8_t session, uint16_t allocationLength, bool useBCD);
    bool doReadSubChannel(bool time, bool subq, uint8_t parameter, uint8_t track_number, uint16_t allocation_length);


    void cdromGetAudioPlaybackStatus(uint8_t *status, uint32_t *current_lba, bool current_only);

    // Read handling, possible conversion of sector formats
    struct {
        int sector_length_file; // Sector length in backing image file
        int sector_length_out; // Sector length output to IDE bus
        int sector_data_skip; // Skip number of bytes at beginning of file sector
        int sector_data_length; // Number of bytes of sector data to copy
        bool add_fake_headers;
        bool field_q_subchannel;
        CUETrackInfo trackinfo;
        uint32_t start_lba;
        uint32_t sectors_done;
    } m_cd_read_format;

    // Read handling and sector format translation if needed
    virtual bool doRead(uint32_t lba, uint32_t transfer_len) override;
    bool doReadCD(uint32_t lba, uint32_t length, uint8_t sector_type,
                  uint8_t main_channel, uint8_t sub_channel, bool data_only);
    virtual ssize_t read_callback(const uint8_t *data, size_t blocksize, size_t num_blocks);

    // Access data from CUE sheet, or dummy data if no cue sheet provided
    char m_cuesheet[1024];
    CUEParser m_cueparser;
    bool loadAndValidateCueSheet(const char *cuesheetname);
    bool getFirstLastTrackInfo(CUETrackInfo &first, CUETrackInfo &last);
    uint32_t getLeadOutLBA(const CUETrackInfo* lasttrack);
    CUETrackInfo getTrackFromLBA(uint32_t lba);

    // ATAPI configuration pages
    virtual size_t atapi_get_configuration(uint8_t return_type, uint16_t feature, uint8_t *buffer, size_t max_bytes) override;

    // ATAPI mode pages
    virtual size_t atapi_get_mode_page(uint8_t page_ctrl, uint8_t page_idx, uint8_t *buffer, size_t max_bytes) override;
    virtual void atapi_set_mode_page(uint8_t page_ctrl, uint8_t page_idx, const uint8_t *buffer, size_t length) override;

    // Event status notification handling
    struct 
    {
        esn_event_t event;
        esn_class_request_t request;
        esn_event_t current_event;
    } m_esn;

    // Event Status Notification statemachine
    void esn_next_event();

    // Audio playback handling

    bool doPauseResumeAudio(bool resume);
    // \todo remove from public and enable here
    // bool doPlayAudio(uint32_t lba, uint32_t length);
};
