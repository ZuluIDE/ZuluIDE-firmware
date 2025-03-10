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
 * Under Section 7 of GPL version 3, you are granted additional
 * permissions described in the ZuluIDE Hardware Support Library Exception
 * (GPL-3.0_HSL_Exception.md), as published by Rabbit Hole Computing™.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
**/

#include <stdint.h>
#include <string.h>
#include <pico/multicore.h>
#include <pico/platform/cpu_regs.h>
#include <hardware/pio.h>
#include <hardware/gpio.h>
#include <hardware/dma.h>
#include <SdFat.h>
#include "ZuluIDE.h"
#include "rp2350_sniffer.pio.h"

/* These settings can be overridden in platformio.ini */

// Buffer used for raw capture data from PIO (must be multiple of 4)
#ifndef SNIFFER_BLOCKSIZE
#define SNIFFER_BLOCKSIZE 4096
#endif

// Number of blocks, must be power of 2
#ifndef SNIFFER_BLOCKCOUNT
#define SNIFFER_BLOCKCOUNT 16
#endif

// DMA channel for transfer of data from PIO to RAM
#ifndef SNIFFER_DMACH
#define SNIFFER_DMACH 5
#endif

// DMA channel for reconfiguring first DMA channel
#ifndef SNIFFER_DMACH_B
#define SNIFFER_DMACH_B 6
#endif

// PIO block used for capture
#ifndef SNIFFER_PIO
#define SNIFFER_PIO pio2
#endif

// PIO state machine used
#ifndef SNIFFER_PIO_SM
#define SNIFFER_PIO_SM 3
#endif

// Millisecond interval to report data and to sync to SD card
#ifndef SNIFFER_SYNC_INTERVAL
#define SNIFFER_SYNC_INTERVAL 2000
#endif

static_assert(SNIFFER_BLOCKSIZE % 4 == 0, "Buffer size must be divisible by 16");
static_assert((SNIFFER_BLOCKCOUNT & (SNIFFER_BLOCKCOUNT - 1)) == 0, "Block count must be power of 2");

// DMA transfers captured transitions to this buffer
#define SNIFFER_BLOCKSIZE_WORDS (SNIFFER_BLOCKSIZE / 4)
static uint32_t g_sniffer_buf[SNIFFER_BLOCKCOUNT][SNIFFER_BLOCKSIZE_WORDS];

bool g_rp2350_passive_sniffer;

static struct {
    bool channels_claimed;
    uint32_t offset_sniffer;
    FsFile file;
    uint32_t sync_time;

    // Total blocks written to SD card so far
    uint32_t total_blocks;

    bool should_sync;
    uint32_t writes_since_sync;

    uint32_t total_bytes;
    uint32_t overruns;

    // Number of blocks written, used by sd write callback
    uint32_t sd_blocks_complete;
} g_sniffer;

// These buffer pointers are used to retrigger DMA from
// the start when it reaches the end.
// Half of the entries are nullptr, which stops DMA from overwriting
// pending data.
#define DMA_BLOCKPTR_COUNT (SNIFFER_BLOCKCOUNT * 2)
__attribute__((aligned(sizeof(uint32_t*) * DMA_BLOCKPTR_COUNT)))
uint32_t *g_sniffer_dma_dest_blocks[DMA_BLOCKPTR_COUNT];

bool rp2350_sniffer_init(const char *filename, bool passive)
{
    g_rp2350_passive_sniffer = passive;

    if (!g_sniffer.channels_claimed)
    {
        pio_sm_claim(SNIFFER_PIO, SNIFFER_PIO_SM);
        dma_channel_claim(SNIFFER_DMACH);
        dma_channel_claim(SNIFFER_DMACH_B);
        g_sniffer.offset_sniffer = pio_add_program(SNIFFER_PIO, &rp2350_sniffer_program);
        g_sniffer.channels_claimed = true;
    }

    pio_sm_set_enabled(SNIFFER_PIO, SNIFFER_PIO_SM, false);
    dma_channel_abort(SNIFFER_DMACH);
    g_sniffer.file.close();
    g_sniffer.total_blocks = 0;
    g_sniffer.should_sync = false;
    g_sniffer.writes_since_sync = 0;
    g_sniffer.total_bytes = 0;
    g_sniffer.total_blocks = 0;
    g_sniffer.sd_blocks_complete = 0;
    g_sniffer.sync_time = 0;

    g_sniffer.file = SD.open(filename, O_WRONLY | O_CREAT | O_TRUNC);
    if (!g_sniffer.file.isOpen())
    {
        logmsg("-- Failed to open ", filename , " for writing");
        return false;
    }

    {
        pio_sm_config cfg = rp2350_sniffer_program_get_default_config(g_sniffer.offset_sniffer);
        sm_config_set_in_pins(&cfg, IDE_DIOW);
        pio_sm_init(SNIFFER_PIO, SNIFFER_PIO_SM, g_sniffer.offset_sniffer, &cfg);
    }

    // First DMA channel for data transfer
    {
        dma_channel_config cfg = dma_channel_get_default_config(SNIFFER_DMACH);
        channel_config_set_transfer_data_size(&cfg, DMA_SIZE_32);
        channel_config_set_read_increment(&cfg, false);
        channel_config_set_write_increment(&cfg, true);
        channel_config_set_chain_to(&cfg, SNIFFER_DMACH_B);
        channel_config_set_dreq(&cfg, pio_get_dreq(SNIFFER_PIO, SNIFFER_PIO_SM, false));
        dma_channel_configure(SNIFFER_DMACH, &cfg,
            g_sniffer_buf[0],
            &SNIFFER_PIO->rxf[SNIFFER_PIO_SM],
            SNIFFER_BLOCKSIZE_WORDS,
            false);
    }

    // Second DMA channel to retrigger first one from the start of the buffer
    {
        // Fill in pointers for the first set of blocks, and null for blocks after that.
        for (int i = 0; i < SNIFFER_BLOCKCOUNT; i++)
        {
            g_sniffer_dma_dest_blocks[i] = g_sniffer_buf[i];
            g_sniffer_dma_dest_blocks[i + SNIFFER_BLOCKCOUNT] = nullptr;
        }

        dma_channel_config cfg = dma_channel_get_default_config(SNIFFER_DMACH_B);
        channel_config_set_read_increment(&cfg, true);
        channel_config_set_write_increment(&cfg, false);
        channel_config_set_ring(&cfg, false, __builtin_ctz(sizeof(g_sniffer_dma_dest_blocks)));
        channel_config_set_transfer_data_size(&cfg, DMA_SIZE_32);
        dma_channel_configure(SNIFFER_DMACH_B, &cfg,
            &dma_hw->ch[SNIFFER_DMACH].al2_write_addr_trig,
            &g_sniffer_dma_dest_blocks,
            1,
            true);
    }

    // Start encoding with the initial states of the pins
    pio_sm_exec(SNIFFER_PIO, SNIFFER_PIO_SM, pio_encode_set(pio_osr, 30));
    pio_sm_exec(SNIFFER_PIO, SNIFFER_PIO_SM, pio_encode_mov(pio_x, pio_pins));
    pio_sm_exec(SNIFFER_PIO, SNIFFER_PIO_SM, pio_encode_jmp(g_sniffer.offset_sniffer + rp2350_sniffer_offset_change));
    pio_sm_set_enabled(SNIFFER_PIO, SNIFFER_PIO_SM, true);

    return true;
}

// Process new data from DMA while SD card is busy writing
static void sniffer_sd_callback(uint32_t bytes_complete)
{
    uint32_t blocks_complete = bytes_complete / SNIFFER_BLOCKSIZE;
    while (blocks_complete > g_sniffer.sd_blocks_complete)
    {
        // We can release more blocks to DMA
        uint32_t idx = (g_sniffer.total_blocks + SNIFFER_BLOCKCOUNT) % DMA_BLOCKPTR_COUNT;
        uint32_t *blockptr = g_sniffer_buf[idx % SNIFFER_BLOCKCOUNT];
        g_sniffer_dma_dest_blocks[idx] = blockptr;
        g_sniffer.total_blocks++;
        g_sniffer.sd_blocks_complete++;

        // Check if the DMA has paused (causes data loss)
        if (dma_hw->ch[SNIFFER_DMACH].al2_write_addr_trig == 0)
        {
            uint32_t dma_wrpos = (dma_hw->ch[SNIFFER_DMACH_B].al1_read_addr - (uint32_t)g_sniffer_dma_dest_blocks) / sizeof(uint32_t*);
            uint32_t *blockptr = g_sniffer_buf[(dma_wrpos - 1) % SNIFFER_BLOCKCOUNT];

            g_sniffer.overruns++;

            // There was dropped data.
            // Encode a "glitch" that will visually indicate lost data
            const uint32_t glitch[6] = {
                0xF0000000, // All signals low, 1 cycle
                0xFBFF8ACF, // 1 ms pause
                0xF7FFFFFF, // All signals high, 1 cycle
                0xF0000000, // All signals low, 1 cycle
                0xFBFF8ACF, // 1 ms pause
                0xFC000000 | (millis() & 0xFFFFFF), // Timestamp
            };

            memcpy(blockptr, glitch, sizeof(glitch));

            // Resume writing to the block but with less words
            dma_hw->ch[SNIFFER_DMACH].al2_transfer_count = (SNIFFER_BLOCKSIZE - sizeof(glitch)) / 4;
            dma_hw->ch[SNIFFER_DMACH].al2_write_addr_trig = (uint32_t)blockptr + sizeof(glitch);

            // Restore block size for next transfer
            dma_hw->ch[SNIFFER_DMACH].al2_transfer_count = SNIFFER_BLOCKSIZE_WORDS;
        }
    }
}

void rp2350_sniffer_poll()
{
    if (!g_sdcard_present) g_sniffer.file.close();
    if (!g_sniffer.file.isOpen()) return;

    // Process data from DMA until we drain the buffer or iteration limit fills
    for (int itercount = 0; itercount < 16; itercount++)
    {
        // Do we have new blocks for writing to SD card
        uint32_t dma_wrpos = (dma_hw->ch[SNIFFER_DMACH_B].al1_read_addr - (uint32_t)g_sniffer_dma_dest_blocks) / sizeof(uint32_t*);
        uint32_t cpu_rdpos = (g_sniffer.total_blocks % DMA_BLOCKPTR_COUNT);
        uint32_t readpos = (g_sniffer.total_blocks % SNIFFER_BLOCKCOUNT);
        uint32_t available = (dma_wrpos - cpu_rdpos - 1) % DMA_BLOCKPTR_COUNT;

        if (available > 0)
        {
            if (readpos + available > SNIFFER_BLOCKCOUNT)
            {
                // Access would wrap around the buffer end, process in two parts
                available = SNIFFER_BLOCKCOUNT - readpos;
            }

            // Remove blocks from DMA availability
            for (int i = 0; i < available; i++)
            {
                uint32_t idx = (g_sniffer.total_blocks + i) % DMA_BLOCKPTR_COUNT;
                g_sniffer_dma_dest_blocks[idx] = nullptr;
            }

            uint8_t *readptr = (uint8_t*)g_sniffer_buf[readpos];
            g_sniffer.sd_blocks_complete = 0;
            size_t to_write = available * SNIFFER_BLOCKSIZE;
            platform_set_sd_callback(sniffer_sd_callback, readptr);
            g_sniffer.file.write(readptr, to_write);
            platform_set_sd_callback(nullptr, nullptr);

            // Finish the write operation and release blocks to DMA
            sniffer_sd_callback(to_write);

            g_sniffer.total_bytes += to_write;
            g_sniffer.writes_since_sync++;
        }
        else if (itercount > 0)
        {
            // DMA is now empty
            break;
        }

        // Synchronize file size
        if (g_sniffer.should_sync)
        {
            if (g_sniffer.writes_since_sync == 0)
            {
                // Write the partially finished block and seek backwards so it will be rewritten once full.
                const uint8_t *readptr = (uint8_t*)g_sniffer_buf[readpos];
                uint32_t to_write = dma_hw->ch[SNIFFER_DMACH].al1_write_addr - (uint32_t)readptr;
                if (to_write < SNIFFER_BLOCKSIZE)
                {
                    uint64_t pos = g_sniffer.file.curPosition();
                    g_sniffer.file.write(readptr, to_write);
                    g_sniffer.file.seek(pos);
                }
            }

            g_sniffer.file.flush();
            g_sniffer.file.sync();
            g_sniffer.should_sync = false;
            g_sniffer.writes_since_sync = 0;
            g_sniffer.sync_time = millis();
        }
    }

    if (!g_sniffer.should_sync && (uint32_t)(millis() - g_sniffer.sync_time) > SNIFFER_SYNC_INTERVAL)
    {
        logmsg("-- Bus sniffer status: total ", (int)((g_sniffer.total_bytes + 1023) / 1024), " kB, ",
                (int)g_sniffer.overruns, " buffer overruns");

        g_sniffer.should_sync = true;
    }
}
