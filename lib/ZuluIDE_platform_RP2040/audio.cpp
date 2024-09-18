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
// Table with the number of '1' bits for each index.
// Used for SP/DIF parity calculations.
// Placed in SRAM5 for the second core to use with reduced contention.
const uint8_t snd_parity[256] __attribute__((aligned(256), section(".scratch_y.snd_parity"))) = {
    0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4, 
    1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 
    1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 
    1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 
    3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, 
    1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 
    3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, 
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 
    3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, 
    3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, 
    4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8, };

/*
 * Precomputed biphase-mark patterns for data. For an 8-bit value this has
 * 16-bits in MSB-first order for the correct high/low transitions to
 * represent the data, given an output clocking rate twice the bitrate (so the
 * bits '11' or '00' reflect a zero and '10' or '01' represent a one). Each
 * value below starts with a '1' and will need to be inverted if the last bit
 * of the previous mask was also a '1'. These values can be written to an
 * appropriately configured SPI peripheral to blast biphase data at a
 * receiver.
 * 
 * To facilitate fast lookups this table should be put in SRAM with low
 * contention, aligned to an apppropriate boundry.
 */
const uint16_t biphase[256] __attribute__((aligned(512), section(".scratch_y.biphase"))) = {
    0xCCCC, 0xB333, 0xD333, 0xACCC, 0xCB33, 0xB4CC, 0xD4CC, 0xAB33,
    0xCD33, 0xB2CC, 0xD2CC, 0xAD33, 0xCACC, 0xB533, 0xD533, 0xAACC,
    0xCCB3, 0xB34C, 0xD34C, 0xACB3, 0xCB4C, 0xB4B3, 0xD4B3, 0xAB4C,
    0xCD4C, 0xB2B3, 0xD2B3, 0xAD4C, 0xCAB3, 0xB54C, 0xD54C, 0xAAB3,
    0xCCD3, 0xB32C, 0xD32C, 0xACD3, 0xCB2C, 0xB4D3, 0xD4D3, 0xAB2C,
    0xCD2C, 0xB2D3, 0xD2D3, 0xAD2C, 0xCAD3, 0xB52C, 0xD52C, 0xAAD3,
    0xCCAC, 0xB353, 0xD353, 0xACAC, 0xCB53, 0xB4AC, 0xD4AC, 0xAB53,
    0xCD53, 0xB2AC, 0xD2AC, 0xAD53, 0xCAAC, 0xB553, 0xD553, 0xAAAC,
    0xCCCB, 0xB334, 0xD334, 0xACCB, 0xCB34, 0xB4CB, 0xD4CB, 0xAB34,
    0xCD34, 0xB2CB, 0xD2CB, 0xAD34, 0xCACB, 0xB534, 0xD534, 0xAACB,
    0xCCB4, 0xB34B, 0xD34B, 0xACB4, 0xCB4B, 0xB4B4, 0xD4B4, 0xAB4B,
    0xCD4B, 0xB2B4, 0xD2B4, 0xAD4B, 0xCAB4, 0xB54B, 0xD54B, 0xAAB4,
    0xCCD4, 0xB32B, 0xD32B, 0xACD4, 0xCB2B, 0xB4D4, 0xD4D4, 0xAB2B,
    0xCD2B, 0xB2D4, 0xD2D4, 0xAD2B, 0xCAD4, 0xB52B, 0xD52B, 0xAAD4,
    0xCCAB, 0xB354, 0xD354, 0xACAB, 0xCB54, 0xB4AB, 0xD4AB, 0xAB54,
    0xCD54, 0xB2AB, 0xD2AB, 0xAD54, 0xCAAB, 0xB554, 0xD554, 0xAAAB,
    0xCCCD, 0xB332, 0xD332, 0xACCD, 0xCB32, 0xB4CD, 0xD4CD, 0xAB32,
    0xCD32, 0xB2CD, 0xD2CD, 0xAD32, 0xCACD, 0xB532, 0xD532, 0xAACD,
    0xCCB2, 0xB34D, 0xD34D, 0xACB2, 0xCB4D, 0xB4B2, 0xD4B2, 0xAB4D,
    0xCD4D, 0xB2B2, 0xD2B2, 0xAD4D, 0xCAB2, 0xB54D, 0xD54D, 0xAAB2,
    0xCCD2, 0xB32D, 0xD32D, 0xACD2, 0xCB2D, 0xB4D2, 0xD4D2, 0xAB2D,
    0xCD2D, 0xB2D2, 0xD2D2, 0xAD2D, 0xCAD2, 0xB52D, 0xD52D, 0xAAD2,
    0xCCAD, 0xB352, 0xD352, 0xACAD, 0xCB52, 0xB4AD, 0xD4AD, 0xAB52,
    0xCD52, 0xB2AD, 0xD2AD, 0xAD52, 0xCAAD, 0xB552, 0xD552, 0xAAAD,
    0xCCCA, 0xB335, 0xD335, 0xACCA, 0xCB35, 0xB4CA, 0xD4CA, 0xAB35,
    0xCD35, 0xB2CA, 0xD2CA, 0xAD35, 0xCACA, 0xB535, 0xD535, 0xAACA,
    0xCCB5, 0xB34A, 0xD34A, 0xACB5, 0xCB4A, 0xB4B5, 0xD4B5, 0xAB4A,
    0xCD4A, 0xB2B5, 0xD2B5, 0xAD4A, 0xCAB5, 0xB54A, 0xD54A, 0xAAB5,
    0xCCD5, 0xB32A, 0xD32A, 0xACD5, 0xCB2A, 0xB4D5, 0xD4D5, 0xAB2A,
    0xCD2A, 0xB2D5, 0xD2D5, 0xAD2A, 0xCAD5, 0xB52A, 0xD52A, 0xAAD5,
    0xCCAA, 0xB355, 0xD355, 0xACAA, 0xCB55, 0xB4AA, 0xD4AA, 0xAB55,
    0xCD55, 0xB2AA, 0xD2AA, 0xAD55, 0xCAAA, 0xB555, 0xD555, 0xAAAA };
/*
 * Biphase frame headers for SP/DIF, including the special bit framing
 * errors used to detect (sub)frame start conditions. See above table
 * for details.
 */
const uint16_t x_preamble = 0xE2CC;
const uint16_t y_preamble = 0xE4CC;
const uint16_t z_preamble = 0xE8CC;

// DMA configuration info
static dma_channel_config snd_dma_a_cfg;
static dma_channel_config snd_dma_b_cfg;

// some chonky buffers to store audio samples
static uint8_t sample_buf_a[AUDIO_BUFFER_SIZE];
static uint8_t sample_buf_b[AUDIO_BUFFER_SIZE];

// tracking for the state of the above buffers
enum bufstate { STALE, FILLING, READY };
static volatile bufstate sbufst_a = STALE;
static volatile bufstate sbufst_b = STALE;
enum bufselect { A, B };
static bufselect sbufsel = A;
static uint16_t sbufpos = 0;
static uint8_t sbufswap = 0;

// buffers for storing biphase patterns
#define AUDIO_OUT_BUFFER_SIZE (AUDIO_BUFFER_SIZE / 4)
static uint32_t output_buf_a[AUDIO_OUT_BUFFER_SIZE];
static uint32_t output_buf_b[AUDIO_OUT_BUFFER_SIZE];

// tracking for audio playback
static bool audio_idle = true;
static bool audio_playing = false;
static volatile bool audio_paused = false;
static uint64_t fpos;
static uint32_t fleft;

// historical playback status information
static audio_status_code audio_last_status = ASC_NO_STATUS;
// volume information for targets
static volatile uint16_t volume[2] = {DEFAULT_VOLUME_LEVEL, DEFAULT_VOLUME_LEVEL};
static volatile uint16_t channel = AUDIO_CHANNEL_ENABLE_MASK;

// mechanism for cleanly stopping DMA units
static volatile bool audio_stopping = false;

// trackers for the below function call
static uint16_t sfcnt = 0; // sub-frame count; 2 per frame, 192 frames/block
static uint8_t invert = 0; // biphase encode help: set if last wire bit was '1'

/*
 * Translates 16-bit stereo sound samples to biphase wire patterns for the
 * SPI peripheral. Produces 8 patterns (128 bits, or 1 SP/DIF frame) per pair
 * of input samples. Provided length is the total number of sample bytes present,
 * _twice_ the number of samples (little-endian order assumed)
 * 
 * This function operates with side-effects and is not safe to call from both
 * cores. It must also be called in the same order data is intended to be
 * output.
 */
static void snd_encode(int16_t* samples, int16_t* output_buf, uint16_t len, uint8_t swap) {
    // enable or disable based on the channel information for both output
    // ports, where the high byte and mask control the right channel, and
    // the low control the left channel
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
    if (sbufsel == A) {
        if (sbufst_a == READY) {
            snd_encode((int16_t *)(sample_buf_a), (int16_t*)(output_buf_a), AUDIO_BUFFER_SIZE/2, sbufswap);
            sbufsel = B;
            sbufst_a = STALE;
        } else {
            snd_encode(nullptr, (int16_t*)output_buf_a, AUDIO_BUFFER_SIZE/2, sbufswap);
        }
    } else {
        if (sbufst_b == READY) {
            snd_encode((int16_t *)sample_buf_b, (int16_t*)output_buf_a, AUDIO_BUFFER_SIZE/2, sbufswap);
            sbufsel = A;
            sbufst_b = STALE;
        } else {
            snd_encode(nullptr, (int16_t*)output_buf_a, AUDIO_BUFFER_SIZE/2, sbufswap);
        }
    }
}
static void snd_process_b() {
    // clone of above for the other wire buffer
    if (sbufsel == A) {
        if (sbufst_a == READY) {
            snd_encode((int16_t *)sample_buf_a, (int16_t*)(output_buf_b), AUDIO_BUFFER_SIZE/2, sbufswap);
            sbufsel = B;
            sbufpos = 0;
            sbufst_a = STALE;
        } else {
            snd_encode(nullptr, (int16_t*)output_buf_a, AUDIO_BUFFER_SIZE/2, sbufswap);
        }
    } else {
        if (sbufst_b == READY) {
            snd_encode((int16_t *)sample_buf_b, (int16_t*)output_buf_b, AUDIO_BUFFER_SIZE/2, sbufswap);
            sbufsel = A;
            sbufpos = 0;
            sbufst_b = STALE;
        } else {
            snd_encode(nullptr, (int16_t*)output_buf_a, AUDIO_BUFFER_SIZE/2, sbufswap);
        }
    }
}

// Allows execution on Core1 via function pointers. Each function can take
// no parameters and should return nothing, operating via side-effects only.
static void core1_handler() {
    while (1) {
        void (*function)() = (void (*)()) multicore_fifo_pop_blocking();
        (*function)();
    }
}

/* ------------------------------------------------------------------------ */
/* ---------- VISIBLE FUNCTIONS ------------------------------------------- */
/* ------------------------------------------------------------------------ */
extern "C"
{
static void audio_dma_irq() {
    if (dma_hw->intr & (1 << SOUND_DMA_CHA)) {
        dma_hw->ints0 = (1 << SOUND_DMA_CHA);
        multicore_fifo_push_blocking((uintptr_t) &snd_process_a);
        if (audio_stopping) {
            channel_config_set_chain_to(&snd_dma_a_cfg, SOUND_DMA_CHA);
        }
        dma_channel_configure(SOUND_DMA_CHA,
                &snd_dma_a_cfg,
                i2s.getPioFIFOAddr(),
                &output_buf_a,
                AUDIO_OUT_BUFFER_SIZE,
                false);
    } else if (dma_hw->intr & (1 << SOUND_DMA_CHB)) {
        dma_hw->ints0 = (1 << SOUND_DMA_CHB);
        multicore_fifo_push_blocking((uintptr_t) &snd_process_b);
        if (audio_stopping) {
            channel_config_set_chain_to(&snd_dma_b_cfg, SOUND_DMA_CHB);
        }
        dma_channel_configure(SOUND_DMA_CHB,
                &snd_dma_b_cfg,
                i2s.getPioFIFOAddr(),
                &output_buf_b,
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
    dbgmsg("Disabling Arduino Core 1 setup to enable the Audio Ouput Core 1 handler");
    rp2040.idleOtherCore();
    multicore_reset_core1();
    // setup Arduino-Pico I2S library
    i2s.setBCLK(GPIO_I2S_BCLK);
    i2s.setDATA(GPIO_I2S_DOUT);
    i2s.setBitsPerSample(16);
    i2s.setDivider(96, 0); // 44.1KHz to the nearest integer with a sys clk of 135.43MHz
    i2s.begin();
    dma_channel_claim(SOUND_DMA_CHA);
	dma_channel_claim(SOUND_DMA_CHB);

    irq_set_exclusive_handler(DMA_IRQ_0, audio_dma_irq);
    irq_set_enabled(DMA_IRQ_0, true);

    logmsg("Starting Core1 for audio");
    multicore_launch_core1(core1_handler);
}

void audio_poll() {
    FsFile *audio_file = g_ide_imagefile.direct_file();
    if (!audio_is_active()) return;
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
        sbufst_a = READY;
    } else if (sbufst_b == FILLING) {
        sbufst_b = READY;
    }
}

bool audio_play(uint64_t start, uint64_t end, bool swap) {
    FsFile *audio_file = g_ide_imagefile.direct_file();
    // stop any existing playback first
    if (audio_is_active()) audio_stop();

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
    fleft = end - start;
    if (fleft <= 2 * AUDIO_BUFFER_SIZE) {
        logmsg("File playback request (", start, ":", end, ") too short");
        return false;
    }

    // read in initial sample buffers
    if (!audio_file->seek(start)) {
        logmsg("Sample file failed start seek to ", start);
        return false;
    }
    if (audio_file->read(sample_buf_a, AUDIO_BUFFER_SIZE) != AUDIO_BUFFER_SIZE) {
        logmsg("File playback start returned fewer bytes than allowed");
        return false;
    }
    if (audio_file->read(sample_buf_b, AUDIO_BUFFER_SIZE) != AUDIO_BUFFER_SIZE) {
        logmsg("File playback start returned fewer bytes than allowed");
        return false;
    }

    // prepare initial tracking state
    fpos = audio_file->position();
    fleft -= AUDIO_BUFFER_SIZE * 2;
    sbufsel = A;
    sbufpos = 0;
    sbufswap = swap;
    sbufst_a = READY;
    sbufst_b = READY;
    audio_last_status = ASC_PLAYING;
    audio_paused = false;
    audio_playing = true;
    audio_idle = false;


    // prepare the wire buffers
    for (uint16_t i = 0; i < AUDIO_OUT_BUFFER_SIZE; i++) {
        output_buf_a[i] = 0;
        output_buf_b[i] = 0;
    }
    sfcnt = 0;
    invert = 0;

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
			&output_buf_a, AUDIO_OUT_BUFFER_SIZE, false);
    dma_channel_set_irq0_enabled(SOUND_DMA_CHA, true);
	snd_dma_b_cfg = dma_channel_get_default_config(SOUND_DMA_CHB);
	channel_config_set_transfer_data_size(&snd_dma_b_cfg, DMA_SIZE_32);
	channel_config_set_dreq(&snd_dma_b_cfg, i2s.getPioDreq());
	channel_config_set_read_increment(&snd_dma_b_cfg, true);
	channel_config_set_chain_to(&snd_dma_b_cfg, SOUND_DMA_CHA);
    snd_dma_b_cfg.ctrl |= DMA_CH0_CTRL_TRIG_HIGH_PRIORITY_BITS;
	dma_channel_configure(SOUND_DMA_CHB, &snd_dma_b_cfg, i2s.getPioFIFOAddr(),
			&output_buf_b, AUDIO_OUT_BUFFER_SIZE, false);
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
    volume[1] = rvol;
    volume[2] = lvol;
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