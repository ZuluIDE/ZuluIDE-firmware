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

    cpu_freq = 150e6      # CPU frequency for timestamps
    divider = 5           # Divider used for VCD timestamps
    timescale = '33333ps' # VCD timescale (divider / cpu_freq)

    def __init__(self):
        self.reset()

    def reset(self):
        '''Initialize state at start of a file'''
        self.timestamp = 0
        self.pin_states = 0

    def decode(self, data: bytes):
        '''Decode binary data and return a list of tuples:
        [(timedelta, pin_states), ...]
        '''

        result = []
        for word, in struct.iter_unpack("<I", data):
            d = word >> 27
            p = word & 0x07FFFFFF

            if d != 31:
                self.timestamp += 5 * (31 - d)
                self.pin_states = p
                result.append((self.timestamp, self.pin_states))
            elif p < 0x0007FFFF:
                self.timestamp += 5 * (0x0007FFFF - p + 3)
            else:
                self.timestamp += 5 * (0x0007FFFF + 3)

        if result and self.timestamp != result[-1][0]:
            # Store the last timestamp in the capture block
            result.append((self.timestamp, self.pin_states))
        return result

    def format_vcd(self, transitions):
        '''Format tuple of changes into VCD format'''
        result = []
        for timestamp, pin_states in transitions:
            line = '#%d' % (timestamp // self.divider)
            for bit, bitcount, name, symbol in self.pinmap:
                if bitcount == 1:
                    line += " %d%s" % ((pin_states >> bit) & 1, symbol)
                else:
                    bitstring = bin(pin_states >> bit)[2:]
                    bitstring = bitstring.zfill(bitcount)[-bitcount:]
                    line += " b%s %s" % (bitstring, symbol)
            result.append(line)
        return result

    def format_vcd_header(self):
        '''Format header for a VCD file with the signal map.
        Returns a list of lines.'''
        result = []
        result.append('$version ZuluIDE RP2350 Sniffer $end')
        result.append('$timescale %s $end' % self.timescale)
        result.append('$scope module ide $end')

        for bit, bitcount, name, symbol in self.pinmap:
            result.append('$var wire %d %s %s $end' % (bitcount, symbol, name))

        result.append('$upscope $end')
        result.append('$enddefinitions $end')
        return result

    def convert_file(self, infile, outfile, blocksize = 65536):
        '''Convert a complete dump file to VCD format.
        Yields total number of transitions after each block
        '''
        self.reset()

        outfile.write('\n'.join(self.format_vcd_header()) + '\n\n')

        tcount = 0
        while True:
            data = infile.read(blocksize)
            if not data: break

            trans = self.decode(data)
            lines = self.format_vcd(trans)
            outfile.write('\n'.join(lines) + '\n')

            tcount += len(trans)
            yield tcount

if __name__ == '__main__':
    if len(sys.argv) != 2:
        sys.stderr.write("Usage: %s sniff.dat\n" % sys.argv[0])
        sys.exit(1)

    infilename = sys.argv[1]
    outfilename = os.path.splitext(infilename)[0] + os.path.extsep + 'vcd'

    print("Writing to %s" % outfilename)

    infile = open(infilename, 'rb')
    outfile = open(outfilename, 'w')
    decoder = SniffDecoder()

    total_trans = os.path.getsize(infilename) / 4 # Approximate
    for tcount in decoder.convert_file(infile, outfile):
        sys.stdout.write("Progress: %d/%d transitions (%d %%)\r" % (tcount, total_trans, 100 * tcount // total_trans))
        sys.stdout.flush()

    print("Done, total %d transitions                                " % tcount)
