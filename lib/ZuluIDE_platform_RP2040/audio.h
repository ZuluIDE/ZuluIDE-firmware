/**
 * Copyright (C) 2023 saybur
 * Copyright (C) 2024 Rabbit Hole Computing LLC
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
#ifdef ENABLE_AUDIO_OUTPUT

#include <stdint.h>


// i2s PIO settings
#define I2S_PIO_HW pio0_hw
#define I2S_PIO_SM 1

// audio subsystem DMA channels
#define SOUND_DMA_CHA 6
#define SOUND_DMA_CHB 7

// size of the two audio sample buffers, in bytes
// #define AUDIO_BUFFER_SIZE 8192 // reduce memory usage
#define AUDIO_BUFFER_SIZE 2352 * 4
/**
 * Indicates if the audio subsystem is actively streaming, including if it is
 * sending silent data during sample stall events.
 *
 * \return true if audio streaming is active, false otherwise.
 */
bool audio_is_active();

/**
 * Initializes the audio subsystem. Should be called only once, toward the end
 * of platform_late_init().
 */
void audio_init();

/**
 * Set Max volume percentage
 * \param max_vol Max volume from 100 to 0 (mute)
 */
void audio_set_max_volume(uint8_t max_vol);

/**
 * Called from platform_poll() to fill sample buffer(s) if needed.
 */
void audio_poll();

#endif // ENABLE_AUDIO_OUTPUT
