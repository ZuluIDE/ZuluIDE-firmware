/** 
 * ZuluSCSI™ - Copyright (c) 2022 Rabbit Hole Computing™
 * 
 * ZuluSCSI™ firmware is licensed under the GPL version 3 or any later version. 
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

#include <stdint.h>
#include <CUEParser.h>
/*
 * Starting volume level for audio output, with 0 being muted and 255 being
 * max volume. SCSI-2 says this should be 25% of maximum by default, MMC-1
 * says 100%. Testing shows this tends to be obnoxious at high volumes, so
 * go with SCSI-2.
 *
 * This implementation uses the high byte for output port 1 and the low byte
 * for port 0. The two values are averaged to determine final volume level.
 */
#define DEFAULT_VOLUME_LEVEL 0xFF

/*
 * Defines the 'enable' masks for the two audio output ports of each device.
 * If this mask is matched with audio_get_channel() the relevant port will
 * have audio output to it, otherwise it will be muted, regardless of the
 * volume level.
 */
#define AUDIO_CHANNEL_ENABLE_MASK 0x0201

/*
 * Status codes for audio playback, matching the SCSI 'audio status codes'.
 *
 * The first two are for a live condition and will be returned repeatedly. The
 * following two reflect a historical condition and are only returned once.
 */
enum audio_status_code {
    ASC_PLAYING = 0x11,
    ASC_PAUSED = 0x12,
    ASC_COMPLETED = 0x13,
    ASC_ERRORED = 0x14,
    ASC_NO_STATUS = 0x15
};

/**
 * Indicates whether there is an active playback event for a given target.
 *
 * Note: this does not consider pause/resume events: even if audio is paused
 * this will indicate playback is in progress.
 *
 * \return       True if playback in progress, false if playback idle.
 */
bool audio_is_playing();

/**
 * Begins audio playback for a file.
 *
 * \param img    Pointer to the image containing PCM samples to play.
 * \param start  LBA playback position where playback will begin, inclusive.
 * \param length Number of sectors till end of playback.
 * \param swap   If false, little-endian sample order, otherwise big-endian.
 * \return       True if successful, false otherwise.
 */
bool audio_play(uint32_t start, uint32_t length, bool swap);

/**
 * Pauses audio playback. This may be delayed slightly to allow sample buffers
 * to purge.
 *
 * \param pause  If true, pause, otherwise resume.
 * \return       True if operation changed audio output, false if no change.
 */
bool audio_set_paused(bool pause);

/**
 * Stops audio playback.
 */
void audio_stop();

/**
 * Provides SCSI 'audio status code' for the given target. Depending on the
 * code this operation may produce side-effects, see the enum for details.
 *
 * \return      The matching audio status code.
 */
audio_status_code audio_get_status_code();

/**
 * Gets the current volume level for a target. This is a pair of 8-bit values
 * ranging from 0-255 that are averaged together to determine the final output
 * level, where 0 is muted and 255 is maximum volume. The high byte corresponds
 * to 0x0E channel 1 and the low byte to 0x0E channel 0. See the spec's mode
 * page documentation for more details.
 *
 * \return      The matching volume level.
 */
uint16_t audio_get_volume();

/**
 * Sets the volume level for a target, as above. See 0x0E mode page for more.
 *
 * \param l_vol   The new volume level for the left channel.
 * \param r_vol   The new volume level for the right channel.
 */
void audio_set_volume(uint8_t l_vol, uint8_t r_vol);

/**
 * Gets the 0x0E channel information for both audio ports. The high byte
 * corresponds to port 1 and the low byte to port 0. If the bits defined in
 * AUDIO_CHANNEL_ENABLE_MASK are not set for the respective ports, that
 * output will be muted, regardless of volume set.
 *
 * \return      The channel information.
 */
uint16_t audio_get_channel();

/**
 * Sets the 0x0E channel information for a target, as above. See 0x0E mode
 * page for more.
 *
 * \param chn   The new channel information.
 */
void audio_set_channel(uint16_t chn);

/**
 * Gets the lba position audio playback audio
 * 
 * \return byte position in the audio image
*/
uint32_t audio_get_lba_position();

/**
 * Sets the playback position in the audio image via the lba
 * 
*/
void audio_set_file_position(uint32_t lba);


/**
 * Sets the cue_parser
 * cue_parser - the cue parser in use
 * filename - the filename for the bini file for a non directory bin/cue combination
 */
void audio_set_cue_parser(CUEParser *cue_parser, FsFile *file);