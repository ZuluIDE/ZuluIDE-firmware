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

#ifndef SNIFFER_BUFSIZE
#define SNIFFER_BUFSIZE 32768
#endif

#ifndef SNIFFER_DMACH
#define SNIFFER_DMACH 6
#endif

#ifndef SNIFFER_PIO
#define SNIFFER_PIO pio2
#endif

#ifndef SNIFFER_PIO_SM
#define SNIFFER_PIO_SM 3
#endif

#ifndef SNIFFER_SYNC_INTERVAL
#define SNIFFER_SYNC_INTERVAL 2000
#endif

static_assert(SNIFFER_BUFSIZE % 4 == 0, "Buffer size must be divisible by 4");
static_assert((SNIFFER_BUFSIZE & (SNIFFER_BUFSIZE - 1)) == 0, "Buffer size must be a power of two");

#define SNIFFER_WORDS (SNIFFER_BUFSIZE / 4)
__attribute__((aligned(SNIFFER_BUFSIZE)))
uint32_t g_sniffer_buf[SNIFFER_WORDS];

bool g_rp2350_passive_sniffer;

static struct {
    bool channels_claimed;
    uint32_t offset_sniffer;
    FsFile file;
    uint32_t read_pos;
    uint32_t sync_time;

    uint32_t total_bytes;
    uint32_t overruns;
} g_sniffer;

bool rp2350_sniffer_init(const char *filename, bool passive)
{
    g_rp2350_passive_sniffer = passive;

    if (!g_sniffer.channels_claimed)
    {
        pio_sm_claim(SNIFFER_PIO, SNIFFER_PIO_SM);
        dma_channel_claim(SNIFFER_DMACH);
        g_sniffer.offset_sniffer = pio_add_program(SNIFFER_PIO, &rp2350_sniffer_program);
        g_sniffer.channels_claimed = true;
    }

    pio_sm_set_enabled(SNIFFER_PIO, SNIFFER_PIO_SM, false);
    dma_channel_abort(SNIFFER_DMACH);
    g_sniffer.file.close();
    g_sniffer.read_pos = 0;

    g_sniffer.file = SD.open(filename, O_WRONLY | O_CREAT | O_TRUNC);
    if (!g_sniffer.file.isOpen())
    {
        logmsg("-- Failed to open ", filename , " for writing");
        // return false;
    }

    {
        pio_sm_config cfg = rp2350_sniffer_program_get_default_config(g_sniffer.offset_sniffer);
        sm_config_set_in_pins(&cfg, IDE_DIOW);
        pio_sm_init(SNIFFER_PIO, SNIFFER_PIO_SM, g_sniffer.offset_sniffer, &cfg);
    }

    {
        dma_channel_config cfg = dma_channel_get_default_config(SNIFFER_DMACH);
        channel_config_set_transfer_data_size(&cfg, DMA_SIZE_32);
        channel_config_set_read_increment(&cfg, false);
        channel_config_set_write_increment(&cfg, true);
        channel_config_set_ring(&cfg, true, __builtin_ctz(SNIFFER_BUFSIZE));
        channel_config_set_dreq(&cfg, pio_get_dreq(SNIFFER_PIO, SNIFFER_PIO_SM, false));
        dma_channel_configure(SNIFFER_DMACH, &cfg,
            g_sniffer_buf,
            &SNIFFER_PIO->rxf[SNIFFER_PIO_SM],
            SNIFFER_WORDS | (1 << 28), // Retrigger this channel after transfer circulates
            true);
    }

    g_sniffer_buf[(g_sniffer.read_pos - 1) & (SNIFFER_WORDS - 1)] = 0xDEADBEEF; // Store sentinel to detect DMA overrun

    // Start encoding with the initial states of the pins
    pio_sm_exec(SNIFFER_PIO, SNIFFER_PIO_SM, pio_encode_set(pio_osr, 30));
    pio_sm_exec(SNIFFER_PIO, SNIFFER_PIO_SM, pio_encode_mov(pio_x, pio_pins));
    pio_sm_exec(SNIFFER_PIO, SNIFFER_PIO_SM, pio_encode_jmp(g_sniffer.offset_sniffer + rp2350_sniffer_offset_change));
    pio_sm_set_enabled(SNIFFER_PIO, SNIFFER_PIO_SM, true);

    return true;
}

#define SD_SECTOR_WORDS (512 / 4)

void rp2350_sniffer_poll()
{
    if (!g_sniffer.file.isOpen()) return;

    uint32_t dma_pos = ((uint32_t)dma_hw->ch[SNIFFER_DMACH].al1_write_addr - (uint32_t)&g_sniffer_buf) / 4;
    uint32_t to_read = (dma_pos - g_sniffer.read_pos) & (SNIFFER_WORDS - 1);
    uint32_t dma_max_trans = SNIFFER_WORDS - to_read;

    if (to_read < SD_SECTOR_WORDS)
    {
        // Always transfer full SD card blocks
        return;
    }

    // We need to stop DMA from modifying the data while we are writing it to SD card
    dma_channel_abort(SNIFFER_DMACH);
    // dbgmsg("To read ", (int)to_read, " dma max ", (int)dma_max_trans,
    //     " dma pos ", (int)dma_pos, " rd pos ", (int)g_sniffer.read_pos,
    //     " dma ptr ", (uint32_t)dma_hw->ch[SNIFFER_DMACH].al1_write_addr,
    //     " read ptr ", (uint32_t)(&g_sniffer_buf[g_sniffer.read_pos]));

    if (dma_max_trans > SD_SECTOR_WORDS)
    {
        // Let it transfer to the part of buffer we are not reading from
        dma_channel_set_trans_count(SNIFFER_DMACH, dma_max_trans - SD_SECTOR_WORDS, true);
    }

    if (g_sniffer_buf[(g_sniffer.read_pos - 1) & (SNIFFER_WORDS - 1)] != 0xDEADBEEF)
    {
        // DMA has overrun and some data has been lost
        g_sniffer.overruns++;
    }

    while (to_read >= SD_SECTOR_WORDS)
    {
        const uint8_t *startptr = (const uint8_t*)&g_sniffer_buf[g_sniffer.read_pos];
        uint32_t blocksize = (to_read / SD_SECTOR_WORDS) * SD_SECTOR_WORDS;

        if (g_sniffer.read_pos + blocksize > SNIFFER_WORDS)
        {
            blocksize = SNIFFER_WORDS - g_sniffer.read_pos;
        }

        g_sniffer.file.write(startptr, blocksize * 4);
        g_sniffer.read_pos = (g_sniffer.read_pos + blocksize) % SNIFFER_WORDS;
        to_read -= blocksize;

        g_sniffer.total_bytes += blocksize * 4;
    }

    g_sniffer_buf[(g_sniffer.read_pos - 1) & (SNIFFER_WORDS - 1)] = 0xDEADBEEF; // Store sentinel to detect DMA overrun

    // Let DMA free run again
    dma_channel_abort(SNIFFER_DMACH);
    dma_channel_set_trans_count(SNIFFER_DMACH, SNIFFER_WORDS | (1 << 28), true);

    if ((uint32_t)(millis() - g_sniffer.sync_time) > SNIFFER_SYNC_INTERVAL)
    {
        logmsg("-- Bus sniffer status: total ", (int)((g_sniffer.total_bytes + 1023) / 1024), " kB, ",
                (int)g_sniffer.overruns, " buffer overruns");

        g_sniffer.file.sync();
        g_sniffer.sync_time = millis();
    }
}
