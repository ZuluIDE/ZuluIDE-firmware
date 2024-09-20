/** 
 * Copyright (C) 2023 saybur
 * Copyright (C) 2024 Rabbit Hole Computing LLC
 * 
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

#ifdef ENABLE_AUDIO_OUTPUT

#include <SdFat.h>
#include <stdbool.h>
#include <hardware/dma.h>
#include <hardware/irq.h>
#include <pico/multicore.h>
#include "audio.h"
#include "ZuluIDE_audio.h"
#include "ZuluIDE_config.h"
#include "ZuluIDE_log.h"
#include "ZuluIDE_platform.h"
#include "ide_imagefile.h"
#include "ide_atapi.h"
#include <ZuluI2S.h>

extern SdFs SD;
extern IDEImageFile g_ide_imagefile;
I2S i2s;

// DMA configuration info
static dma_channel_config snd_dma_a_cfg;
static dma_channel_config snd_dma_b_cfg;

// some chonky buffers to store audio samples
static uint8_t sample_buf_a[AUDIO_BUFFER_SIZE];
static uint8_t sample_buf_b[AUDIO_BUFFER_SIZE];

// tracking for the state of the above buffers
enum bufstate { STALE, FILLING, PROCESSING, READY };
static volatile bufstate sbufst_a = STALE;
static volatile bufstate sbufst_b = STALE;
enum bufselect { A, B };
static bufselect sbufsel = A;

// buffers for storing biphase patterns
#define AUDIO_OUT_BUFFER_SIZE (AUDIO_BUFFER_SIZE / 4)
static uint32_t *output_buf_a = (uint32_t*)sample_buf_a; // [AUDIO_OUT_BUFFER_SIZE];
static uint32_t *output_buf_b = (uint32_t*)sample_buf_b; //[AUDIO_OUT_BUFFER_SIZE];

// tracking for audio playback
static bool audio_idle = true;
static bool audio_playing = false;
static volatile bool audio_paused = false;
static uint64_t fpos;
static uint32_t fleft;

// historical playback status information
static audio_status_code audio_last_status = ASC_NO_STATUS;
// volume information for targets
static volatile uint8_t volume[2] = {DEFAULT_VOLUME_LEVEL, DEFAULT_VOLUME_LEVEL};
static volatile uint16_t channel = AUDIO_CHANNEL_ENABLE_MASK;

// mechanism for cleanly stopping DMA units
static volatile bool audio_stopping = false;

/*
 * I2S format is directly compatible to CD 16-bit audio with left and right channels
 * The only encoding needed is adjusting the volume and muting if one of the channels 
 * is disabled.
 */
static void snd_encode(int16_t* samples, int16_t* output_buf, uint16_t len) {
    uint8_t vol[2] ={volume[0], volume[1]};
    uint16_t chn = channel & AUDIO_CHANNEL_ENABLE_MASK;
    if (!(chn >> 8)) vol[1] = 0;   // right
    if (!(chn & 0xFF)) vol[0] = 0; // left

    for (uint16_t i = 0; i < len; i++ )
    {
        if (samples == nullptr)
            output_buf[i] = 0;
        else
            output_buf[i] = (int16_t)(((int32_t)samples[i]) * (vol[i & 0x01]) / 255);
    }
}

// functions for passing to Core1
static void snd_process_a() {
    snd_encode((int16_t *)(sample_buf_a), (int16_t*)(output_buf_a), AUDIO_BUFFER_SIZE/2);
}
static void snd_process_b() {
    snd_encode((int16_t *)sample_buf_b, (int16_t*)(output_buf_b), AUDIO_BUFFER_SIZE/2);
}

/* ------------------------------------------------------------------------ */
/* ---------- VISIBLE FUNCTIONS ------------------------------------------- */
/* ------------------------------------------------------------------------ */
extern "C"
{
static void audio_dma_irq() {
    if (dma_hw->intr & (1 << SOUND_DMA_CHA)) {
        dma_hw->ints0 = (1 << SOUND_DMA_CHA);
        sbufst_a = STALE;
        // multicore_fifo_push_blocking((uintptr_t) &snd_process_a);
        if (audio_stopping) {
            channel_config_set_chain_to(&snd_dma_a_cfg, SOUND_DMA_CHA);
        }
        dma_channel_configure(SOUND_DMA_CHA,
                &snd_dma_a_cfg,
                i2s.getPioFIFOAddr(),
                output_buf_a,
                AUDIO_OUT_BUFFER_SIZE,
                false);
    } else if (dma_hw->intr & (1 << SOUND_DMA_CHB)) {
        dma_hw->ints0 = (1 << SOUND_DMA_CHB);
        sbufst_b = STALE;
        // multicore_fifo_push_blocking((uintptr_t) &snd_process_b);
        if (audio_stopping) {
            channel_config_set_chain_to(&snd_dma_b_cfg, SOUND_DMA_CHB);
        }
        dma_channel_configure(SOUND_DMA_CHB,
                &snd_dma_b_cfg,
                i2s.getPioFIFOAddr(),
                output_buf_b,
                AUDIO_OUT_BUFFER_SIZE,
                false);
    }
}
}
bool audio_is_active() {
    return !audio_idle;
}

bool audio_is_playing() {
    return audio_playing;
}

void audio_setup() {
    // setup Arduino-Pico I2S library
    i2s.setBCLK(GPIO_I2S_BCLK);
    i2s.setDATA(GPIO_I2S_DOUT);
    i2s.setBitsPerSample(16);
    // 44.1KHz to the nearest integer with a sys clk of 135.43MHz and 2 x 16-bit samples
    // 135.43Mhz / 16 / 2 / 44.1KHz = 95.98 ~= 96
    i2s.setDivider(96, 0); 
    i2s.begin();
    dma_channel_claim(SOUND_DMA_CHA);
	dma_channel_claim(SOUND_DMA_CHB);

    irq_set_exclusive_handler(DMA_IRQ_0, audio_dma_irq);
    irq_set_enabled(DMA_IRQ_0, true);
}

void audio_poll() {
    FsFile *audio_file = g_ide_imagefile.direct_file();
    if (audio_idle) return;
    if (audio_paused) return;
    if (fleft == 0 && sbufst_a == STALE && sbufst_b == STALE) {
        // out of data and ready to stop
        audio_stop();
        return;
    } else if (fleft == 0) {
        // out of data to read but still working on remainder
        return;
    } else if (!audio_file->isOpen()) {
        // closed elsewhere, maybe disk ejected?
        dbgmsg("------ Playback stop due to closed file");
        audio_stop();
        return;
    }

    // are new audio samples needed from the memory card?
    uint8_t* audiobuf;
    if (sbufst_a == STALE) {
        sbufst_a = FILLING;
        audiobuf = sample_buf_a;
    } else if (sbufst_b == STALE) {
        sbufst_b = FILLING;
        audiobuf = sample_buf_b;
    } else {
        // no data needed this time
        return;
    }

    platform_set_sd_callback(NULL, NULL);
    uint16_t toRead = AUDIO_BUFFER_SIZE;
    if (fleft < toRead) toRead = fleft;
    if (audio_file->position() != fpos) {
        // should be uncommon due to SCSI command restrictions on devices
        // playing audio; if this is showing up in logs a different approach
        // will be needed to avoid seek performance issues on FAT32 vols
        dbgmsg("------ Audio seek required");
        if (!audio_file->seek(fpos)) {
            logmsg("Audio error, unable to seek to ", fpos);
        }
    }
    if (audio_file->read(audiobuf, toRead) != toRead) {
        logmsg("Audio sample data read error");
    }
    fpos += toRead;
    fleft -= toRead;

    if (sbufst_a == FILLING) {
        sbufst_a = PROCESSING;
        snd_process_a();
        sbufst_a = READY;
    } else if (sbufst_b == FILLING) {
        sbufst_b = PROCESSING;
        snd_process_b();
        sbufst_b = READY;
    }
}

bool audio_play(uint64_t start, uint64_t end, bool swap) {
    FsFile *audio_file = g_ide_imagefile.direct_file();
    // stop any existing playback first
    if (!audio_idle) audio_stop();

    // dbgmsg("Request to play ('", file, "':", start, ":", end, ")");

    // verify audio file is present and inputs are (somewhat) sane
    if (start >= end) {
        logmsg("Invalid range for audio (", start, ":", end, ")");
        return false;
    }
    platform_set_sd_callback(NULL, NULL);
    if (!audio_file->isOpen()) {
        logmsg("File not open for audio playback");
        return false;
    }
    uint64_t len = audio_file->size();
    if (start > len) {
        logmsg("File playback request start (", start, ":", len, ") outside file bounds");
        return false;
    }
    // truncate playback end to end of file
    // we will not consider this to be an error at the moment
    if (end > len) {
        dbgmsg("------ Truncate audio play request end ", end, " to file size ", len);
        end = len;
    }
    fpos = start;
    fleft = end - start;

    if (fleft <= 2 * AUDIO_BUFFER_SIZE) {
        logmsg("File playback request (", start, ":", end, ") too short");
        return false;
    }

    audio_last_status = ASC_PLAYING;
    audio_paused = false;
    audio_playing = true;
    audio_idle = false;
    // read in initial sample buffers
    sbufst_a = STALE;
    sbufst_b = STALE;
    sbufsel = B;
    audio_poll();
    sbufsel = A;
    audio_poll();

    // setup the two DMA units to hand-off to each other
    // to maintain a stable bitstream these need to run without interruption
	snd_dma_a_cfg = dma_channel_get_default_config(SOUND_DMA_CHA);
	channel_config_set_transfer_data_size(&snd_dma_a_cfg, DMA_SIZE_32);
	channel_config_set_dreq(&snd_dma_a_cfg, i2s.getPioDreq());
	channel_config_set_read_increment(&snd_dma_a_cfg, true);
	channel_config_set_chain_to(&snd_dma_a_cfg, SOUND_DMA_CHB);
    // version of pico-sdk lacks channel_config_set_high_priority()
    snd_dma_a_cfg.ctrl |= DMA_CH0_CTRL_TRIG_HIGH_PRIORITY_BITS;
	dma_channel_configure(SOUND_DMA_CHA, &snd_dma_a_cfg, i2s.getPioFIFOAddr(),
			output_buf_a, AUDIO_OUT_BUFFER_SIZE, false);
    dma_channel_set_irq0_enabled(SOUND_DMA_CHA, true);
	snd_dma_b_cfg = dma_channel_get_default_config(SOUND_DMA_CHB);
	channel_config_set_transfer_data_size(&snd_dma_b_cfg, DMA_SIZE_32);
	channel_config_set_dreq(&snd_dma_b_cfg, i2s.getPioDreq());
	channel_config_set_read_increment(&snd_dma_b_cfg, true);
	channel_config_set_chain_to(&snd_dma_b_cfg, SOUND_DMA_CHA);
    snd_dma_b_cfg.ctrl |= DMA_CH0_CTRL_TRIG_HIGH_PRIORITY_BITS;
	dma_channel_configure(SOUND_DMA_CHB, &snd_dma_b_cfg, i2s.getPioFIFOAddr(),
			output_buf_b, AUDIO_OUT_BUFFER_SIZE, false);
    dma_channel_set_irq0_enabled(SOUND_DMA_CHB, true);

    // ready to go
    dma_channel_start(SOUND_DMA_CHA);
    return true;
}

bool audio_set_paused(bool paused) {
    if (audio_idle) return false;
    else if (audio_paused && paused) return false;
    else if (!audio_paused && !paused) return false;

    audio_paused = paused;

    if (paused) {
        audio_last_status = ASC_PAUSED;
        audio_playing = false;
    } else {
        audio_last_status = ASC_PLAYING;
        audio_playing = true;
    }
    return true;
}

void audio_stop() {
    if (audio_idle) return;

    // to help mute external hardware, send a bunch of '0' samples prior to
    // halting the datastream; easiest way to do this is invalidating the
    // sample buffers, same as if there was a sample data underrun
    sbufst_a = STALE;
    sbufst_b = STALE;

    // then indicate that the streams should no longer chain to one another
    // and wait for them to shut down naturally
    audio_stopping = true;
    while (dma_channel_is_busy(SOUND_DMA_CHA)) tight_loop_contents();
    while (dma_channel_is_busy(SOUND_DMA_CHB)) tight_loop_contents();
    // \todo check if I2S pio is done
    // The way to check is the I2S pio is done would be to check
    // if the fifo is empty and the PIO's program counter is at the first instruction
    // while (spi_is_busy(AUDIO_SPI)) tight_loop_contents();
    audio_stopping = false;

    // idle the subsystem
    audio_last_status = ASC_COMPLETED;
    audio_paused = false;
    audio_playing = false;
    audio_idle = true;
}

audio_status_code audio_get_status_code() {
    audio_status_code tmp = audio_last_status;
    if (tmp == ASC_COMPLETED || tmp == ASC_ERRORED) {
        audio_last_status = ASC_NO_STATUS;
    }
    return tmp;
}

uint16_t audio_get_volume() {
    return volume[0] | (volume[1] << 8);
}

void audio_set_volume(uint8_t lvol, uint8_t rvol) {
    volume[0] = rvol;
    volume[1] = lvol;
}

uint16_t audio_get_channel() {
    return channel;
}

void audio_set_channel(uint16_t chn) {
    channel = chn;
}

uint64_t audio_get_file_position()
{
    return fpos;
}

void audio_set_file_position(uint32_t lba)
{
    fpos = ATAPI_AUDIO_CD_SECTOR_SIZE * (uint64_t)lba;

}
#endif // ENABLE_AUDIO_OUTPUT