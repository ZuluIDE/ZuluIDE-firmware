#!/bin/env python3
#
# ZuluIDE™ - Copyright (c) 2023 Rabbit Hole Computing™
#
# ZuluIDE™ firmware is licensed under the GPL version 3 or any later version. 
#
# https://www.gnu.org/licenses/gpl-3.0.html
# ----
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version. 
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details. 
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

'''This program converts the custom format produced by ZuluIDE RP2350 sniffer
to standard VCD (Value Change Dump) format.
The resulting file can be opened using e.g. PulseView.

See rp2350_sniffer.pio for definition of the encoding format.
'''

import sys
import os.path
import struct
import argparse
import collections
import multiprocessing

class SniffDecoder:
    # Hardware pin mapping
    # Bit, Bitcount, Name, Symbol
    # pinmap = (
    #     (0, 1, 'DIOW', 'W'),
    #     (1, 1, 'DIOR', 'R'),
    #     (2, 3, 'DA', 'A'),
    #     (5, 1, 'CS0', 'c'),
    #     (6, 1, 'CS1', 'C'),
    #     (7, 1, 'DMACK', 'D'),
    #     (8, 16, 'DATA', 'd'),
    #     (24, 1, 'DATA_SEL', 's'),
    #     (25, 1, 'DATA_DIR', 'r'),
    #     (26, 1, 'IORDY', 'i')
    # )

    # Having each signal be 1 bit makes PulseView
    # automatically assign the channels of parallel ATA decoder
    pinmap = (
         (0, 1, 'DIOW', 'W'),
         (1, 1, 'DIOR', 'R'),
         (2, 1, 'DA0', 'A0'),
         (3, 1, 'DA1', 'A1'),
         (4, 1, 'DA2', 'A2'),
         (5, 1, 'CS0', 'C0'),
         (6, 1, 'CS1', 'C1'),
         (7, 1, 'DMACK', 'D'),
         (8,  1, 'D0', 'd0'),
         (9,  1, 'D1', 'd1'),
         (10, 1, 'D2', 'd2'),
         (11, 1, 'D3', 'd3'),
         (12, 1, 'D4', 'd4'),
         (13, 1, 'D5', 'd5'),
         (14, 1, 'D6', 'd6'),
         (15, 1, 'D7', 'd7'),
         (16, 1, 'D8', 'd8'),
         (17, 1, 'D9', 'd9'),
         (18, 1, 'D10', 'd10'),
         (19, 1, 'D11', 'd11'),
         (20, 1, 'D12', 'd12'),
         (21, 1, 'D13', 'd13'),
         (22, 1, 'D14', 'd14'),
         (23, 1, 'D15', 'd15'),
         (24, 1, 'DATA_SEL', 's'),
         (25, 1, 'DATA_DIR', 'r'),
         (26, 1, 'IORDY', 'i')
    )

    cpu_freq = 200e6      # CPU frequency for timestamps
    divider = 5           # Divider used for VCD timestamps
    timescale = '25ns' # VCD timescale (divider / cpu_freq)

    def __init__(self):
        self.reset()

    def reset(self):
        '''Initialize state at start of a file'''
        self.timestamp = 0
        self.systime = 0
        self.systime_ref = 0
        self.last_vcd_timestamp = 0
        self.pin_states = 0
        self.version = 1
        self.logtxt = b''

    def get_systime(self):
        '''Interpolate system time from sample timestamps.'''
        return self.systime + int((self.timestamp - self.systime_ref) * 1000 // self.cpu_freq)

    def decode(self, data: bytes):
        '''Decode binary data and return a list of tuples:
        [(timedelta, pin_states, systime), ...]
        '''

        result = []
        logdatalen = 0

        for word, in struct.iter_unpack("<I", data):
            d = word >> 27
            p = word & 0x07FFFFFF
            max_timedelta = 0x03FFFFFF

            if logdatalen > 0:
                self.logtxt += struct.pack("<I", word).replace(b'\x00', b'')
                logdatalen -= 4
            elif (word >> 24) == 0xFC:
                # System millisecond timestamp for correlating with logs
                self.systime = word & 0xFFFFFF
                self.systime_ref = self.timestamp
            elif (word >> 16) == 0xFF00:
                self.version = word & 0xFF
                self.cpu_freq = ((word >> 8) & 0xFF) * 1e6

                print("Sniffer version %d, CPU frequency %d MHz" % (self.version, self.cpu_freq // 1e6))

                if self.version >= 2:
                    self.divider = 3

                self.timescale = '%dps' % (1e12 * self.divider / self.cpu_freq)

            elif (word >> 16) == 0xFF01:
                logdatalen = (word & 0xFFFF)
                assert logdatalen % 4 == 0
            else:
                if self.version >= 2:
                    if d != 31:
                        self.timestamp += 3 * (32 - d)
                        self.pin_states = p
                    elif p <= max_timedelta:
                        self.timestamp += 3 * (max_timedelta - p + 3)
                    elif word == 0xFFFFFFFF:
                        self.timestamp += 3 * (max_timedelta + 2)
                else:
                    if d != 31:
                        self.timestamp += 5 * (31 - d)
                        self.pin_states = p
                    elif p <= max_timedelta:
                        self.timestamp += 5 * (max_timedelta - p + 3)
                    elif word == 0xFFFFFFFF:
                        self.timestamp += 5 * (max_timedelta + 3)

                result.append((self.timestamp, self.pin_states, self.get_systime()))

        return result

    @classmethod
    def format_vcd_lines(cls, transitions, divider):
        '''Format tuple of changes into VCD format.
        Stateless so that it can be run in worker processes.'''
        result = []
        for timestamp, pin_states, systime in transitions:
            line = '#%d r%0.3f T' % (timestamp // divider, systime / 1000.0)
            for bit, bitcount, name, symbol in cls.pinmap:
                if bitcount == 1:
                    line += " %d%s" % ((pin_states >> bit) & 1, symbol)
                else:
                    bitstring = bin(pin_states >> bit)[2:]
                    bitstring = bitstring.zfill(bitcount)[-bitcount:]
                    line += " b%s %s" % (bitstring, symbol)
            result.append(line)
        return result

    def format_vcd(self, transitions):
        '''Format tuple of changes into VCD format'''
        if transitions:
            self.last_vcd_timestamp = (transitions[-1][0] // self.divider)
        return self.format_vcd_lines(transitions, self.divider)

    def format_vcd_header(self):
        '''Format header for a VCD file with the signal map.
        Returns a list of lines.'''
        result = []
        result.append('$version ZuluIDE RP2350 Sniffer $end')
        result.append('$timescale %s $end' % self.timescale)
        result.append('$scope module ide $end')

        for bit, bitcount, name, symbol in self.pinmap:
            result.append('$var wire %d %s %s $end' % (bitcount, symbol, name))

        result.append('$var real 24 T systime $end')
        result.append('$upscope $end')
        result.append('$enddefinitions $end')
        return result

    def convert_file(self, infile, outfile, blocksize = 65536, jobs = 1):
        '''Convert a complete dump file to VCD format.
        Yields total number of transitions after each block.

        Decoding is sequential because each block depends on the state left
        by the previous one. With jobs > 1, VCD formatting is done in a pool
        of worker processes and the results are written out in order.
        '''
        self.reset()

        if jobs > 1:
            yield from self._convert_file_parallel(infile, outfile, blocksize, jobs)
            return

        tcount = 0
        while True:
            data = infile.read(blocksize)
            if not data: break

            trans = self.decode(data)

            if len(trans) and tcount == 0:
                # Write header after we have decoded the first block, which
                # includes format version information.
                outfile.write('\n'.join(self.format_vcd_header()) + '\n\n')

            lines = self.format_vcd(trans)
            outfile.write('\n'.join(lines) + '\n')

            tcount += len(trans)
            yield tcount

    def _convert_file_parallel(self, infile, outfile, blocksize, jobs):
        with multiprocessing.Pool(jobs) as pool:
            # Limit the number of blocks in flight to bound memory usage
            pending = collections.deque()
            max_pending = jobs * 4
            tcount = 0
            header_written = False
            eof = False

            while not eof or pending:
                while not eof and len(pending) < max_pending:
                    data = infile.read(blocksize)
                    if not data:
                        eof = True
                        break

                    trans = self.decode(data)

                    if len(trans) and not header_written:
                        # Write header after we have decoded the first block, which
                        # includes format version information.
                        outfile.write('\n'.join(self.format_vcd_header()) + '\n\n')
                        header_written = True

                    if trans:
                        self.last_vcd_timestamp = (trans[-1][0] // self.divider)

                    job = pool.apply_async(_format_vcd_block, (trans, self.divider))
                    pending.append((job, len(trans)))

                if pending:
                    job, count = pending.popleft()
                    outfile.write(job.get())
                    tcount += count
                    yield tcount

def _format_vcd_block(transitions, divider):
    '''Worker process entry point for parallel VCD formatting'''
    return '\n'.join(SniffDecoder.format_vcd_lines(transitions, divider)) + '\n'

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description = "Convert ZuluIDE RP2350 sniffer data to VCD")
    parser.add_argument('infile', help = "Sniffer data file, e.g. sniff.dat")
    parser.add_argument('-j', '--jobs', type = int, default = 1,
                        help = "Number of worker processes for VCD formatting (default 1)")
    args = parser.parse_args()

    if args.jobs < 1:
        parser.error("number of jobs must be at least 1")

    infilename = args.infile
    outfilename = os.path.splitext(infilename)[0] + os.path.extsep + 'vcd'
    logfilename = os.path.splitext(infilename)[0] + os.path.extsep + 'txt'

    print("Writing to %s" % outfilename)

    infile = open(infilename, 'rb')
    outfile = open(outfilename, 'w')
    decoder = SniffDecoder()

    total_trans = os.path.getsize(infilename) / 4 # Approximate
    tcount = 0
    for tcount in decoder.convert_file(infile, outfile, jobs = args.jobs):
        sys.stdout.write("Progress: %d/%d transitions (%d %%)\r" % (tcount, total_trans, 100 * tcount // total_trans))
        sys.stdout.flush()

    print("Done, total %d transitions, length %0.1f s,                        " %
          (tcount, decoder.last_vcd_timestamp * decoder.divider / decoder.cpu_freq))

    if decoder.logtxt:
        open(logfilename, 'wb').write(decoder.logtxt)
        print("Saved %d bytes of log text to %s" % (len(decoder.logtxt), logfilename))

    if decoder.last_vcd_timestamp > 1e9:
        print("WARNING: Result file has a large time range, PulseView may take a lot of memory when loading")
        print("         Consider using 'Compress idle periods' in PulseView VCD Import")
