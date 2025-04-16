##
## This file is part of the libsigrokdecode project.
##
## Copyright (C) 2013-2016 Uwe Hermann <uwe@hermann-uwe.de>
##
## This program is free software; you can redistribute it and/or modify
## it under the terms of the GNU General Public License as published by
## the Free Software Foundation; either version 2 of the License, or
## (at your option) any later version.
##
## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with this program; if not, see <http://www.gnu.org/licenses/>.
##

import sigrokdecode as srd
from common.srdhelper import bitpack

class ChannelError(Exception):
    pass

REGADDR_WRITE = {
    0x0E: 'DEVICE_CONTROL',
    0x10: 'DATA',
    0x11: 'FEATURE',
    0x12: 'SECTOR_COUNT',
    0x13: 'LBA_LOW',
    0x14: 'LBA_MID',
    0x15: 'LBA_HIGH',
    0x16: 'DEVICE',
    0x17: 'COMMAND'
}

REGADDR_READ = {
    0x0E: 'ALT_STATUS',
    0x10: 'DATA',
    0x11: 'ERROR',
    0x12: 'SECTOR_COUNT',
    0x13: 'LBA_LOW',
    0x14: 'LBA_MID',
    0x15: 'LBA_HIGH',
    0x16: 'DEVICE',
    0x17: 'STATUS'
}

ATA_COMMANDS = {
    0x00: "NOP",
    0x03: "CFA_REQUEST_EXTENDED_ERROR",
    0x08: "DEVICE_RESET",
    0x20: "READ_SECTORS",
    0x24: "READ_SECTORS_EXT",
    0x25: "READ_DMA_EXT",
    0x26: "READ_DMA_QUEUED_EXT",
    0x27: "READ_NATIVE_MAX_ADDRESS_EXT",
    0x29: "READ_MULTIPLE_EXT",
    0x2F: "READ_LOG_EXT",
    0x30: "WRITE_SECTORS",
    0x34: "WRITE_SECTORS_EXT",
    0x35: "WRITE_DMA_EXT",
    0x36: "WRITE_DMA_QUEUED_EXT",
    0x37: "SET_MAX_ADDRESS_EXT",
    0x38: "CFA_WRITE_SECTORS_WOUT_ERASE",
    0x39: "WRITE_MULTIPLE_EXT",
    0x3F: "WRITE_LOG_EXT",
    0x40: "READ_VERIFY_SECTORS",
    0x42: "READ_VERIFY_SECTORS_EXT",
    0x70: "SEEK",
    0x87: "CFA_TRANSLATE_SECTOR",
    0x90: "EXECUTE_DEVICE_DIAGNOSTIC",
    0x91: "INIT_DEV_PARAMS",
    0x92: "DOWNLOAD_MICROCODE",
    0xA0: "PACKET",
    0xA1: "IDENTIFY_PACKET_DEVICE",
    0xA2: "SERVICE",
    0xB0: "SMART",
    0xB1: "DEVICE_CONFIGURATION",
    0xC0: "CFA_ERASE_SECTORS",
    0xC4: "READ_MULTIPLE",
    0xC5: "WRITE_MULTIPLE",
    0xC6: "SET_MULTIPLE_MODE",
    0xC7: "READ_DMA_QUEUED",
    0xC8: "READ_DMA",
    0xCA: "WRITE_DMA",
    0xCC: "WRITE_DMA_QUEUED",
    0xCD: "CFA_WRITE_MULTIPLE_WOUT_ERASE",
    0xD1: "CHECK_MEDIA_CARD_TYPE",
    0xDA: "GET_MEDIA_STATUS",
    0xDE: "MEDIA_LOCK",
    0xDF: "MEDIA_UNLOCK",
    0xE0: "STANDBY_IMMEDIATE",
    0xE1: "IDLE_IMMEDIATE",
    0xE2: "STANDBY",
    0xE3: "IDLE",
    0xE4: "READ_BUFFER",
    0xE5: "CHECK_POWER_MODE",
    0xE6: "SLEEP",
    0xE7: "FLUSH_CACHE",
    0xE8: "WRITE_BUFFER",
    0xEA: "FLUSH_CACHE_EXT",
    0xEC: "IDENTIFY_DEVICE",
    0xED: "MEDIA_EJECT",
    0xEF: "SET_FEATURES",
    0xF1: "SECURITY_SET_PASSWORD",
    0xF2: "SECURITY_UNLOCK",
    0xF3: "SECURITY_ERASE_PREPARE",
    0xF4: "SECURITY_ERASE_UNIT",
    0xF5: "SECURITY_FREEZE_LOCK",
    0xF6: "SECURITY_DISABLE_PASSWORD",
    0xF8: "READ_NATIVE_MAX_ADDRESS",
    0xF9: "SET_MAX_ADDRESS",
}

ATAPI_COMMANDS = {
    0x00: "TEST UNIT READY",
    0x03: "REQUEST SENSE",
    0x04: "FORMAT UNIT",
    0x12: "INQUIRY",
    0x1B: "START STOP UNIT",
    0x1E: "PREVENT ALLOW MEDIUM REMOVAL",
    0x23: "READ FORMAT CAPACITIES",
    0x25: "READ CAPACITY",
    0x28: "READ (10)",
    0x2A: "WRITE (10)",
    0x2B: "SEEK (10)",
    0x2E: "WRITE AND VERIFY (10)",
    0x2F: "VERIFY (10)",
    0x35: "SYNCHRONIZE CACHE",
    0x3B: "WRITE BUFFER",
    0x3C: "READ BUFFER",
    0x43: "READ TOC/PMA/ATIP",
    0x46: "GET CONFIGURATION",
    0x4A: "GET EVENT STATUS NOTIFICATION",
    0x51: "READ DISC INFORMATION",
    0x52: "READ TRACK INFORMATION",
    0x53: "RESERVE TRACK",
    0x54: "SEND OPC INFORMATION",
    0x55: "MODE SELECT (10)",
    0x58: "REPAIR TRACK",
    0x5A: "MODE SENSE (10)",
    0x5B: "CLOSE TRACK SESSION",
    0x5C: "READ BUFFER CAPACITY",
    0x5D: "SEND CUE SHEET",
    0xA0: "REPORT LUNS",
    0xA1: "BLANK",
    0xA2: "SECURITY PROTOCOL IN",
    0xA3: "SEND KEY",
    0xA4: "REPORT KEY",
    0xA6: "LOAD/UNLOAD MEDIUM",
    0xA7: "SET READ AHEAD",
    0xA8: "READ (12)",
    0xAA: "WRITE (12)",
    0xAB: "READ MEDIA SERIAL NUMBER",
    0xAC: "GET PERFORMANCE",
    0xAD: "READ DISC STRUCTURE",
    0xB5: "SECURITY PROTOCOL OUT",
    0xB6: "SET STREAMING",
    0xB9: "READ CD MSF",
    0xBB: "SET CD SPEED",
    0xBD: "MECHANISM STATUS",
    0xBE: "READ CD",
    0xBF: "SEND DISC STRUCTURE",
}

# Inverted, bit, name
CTRL_MUX_BITS = [
    (True, (1 << 9), "EN_DMARQ"),
    (False, (1 << 10), "OUT_DMARQ"),
    (True, (1 << 11), "EN_INTRQ"),
    (False, (1 << 12), "OUT_INTRQ"),
    (True, (1 << 13), "OUT_DASP"),
    (True, (1 << 14), "OUT_PDIAG"),
]

class Pins:
    '''Indexes must match optional_channels tuple'''
    IOW = 0
    IOR = 1
    DA0 = 2
    DA1 = 3
    DA2 = 4
    CS0 = 5
    CS1 = 6
    D0 = 7
    D8 = 7 + 8
    D15 = 7 + 15
    D16 = 7 + 16
    DATA_SEL = 23

class Annotations:
    '''Indexes must match annotation classes'''
    device = 0
    regwr = 1
    regrd = 2
    datawr = 3
    datard = 4
    cmd = 5
    datatransfer = 6
    status = 7
    atapi = 8
    event = 9

class Decoder(srd.Decoder):
    api_version = 3
    id = 'parallel_ata'
    name = 'Parallel ATA'
    longname = 'Parallel ATA / IDE bus'
    desc = 'Parallel ATA / IDE bus'
    license = 'gplv2+'
    inputs = ['logic']
    outputs = []
    tags = ['Util']
    optional_channels = tuple([
        {'id': 'iow', 'name': 'IOW', 'desc': 'Write strobe'},
        {'id': 'ior', 'name': 'IOR', 'desc': 'Read strobe'},
        {'id': 'da0', 'name': 'DA0', 'desc': 'Device address 0'},
        {'id': 'da1', 'name': 'DA1', 'desc': 'Device address 1'},
        {'id': 'da2', 'name': 'DA2', 'desc': 'Device address 2'},
        {'id': 'cs0', 'name': 'CS0', 'desc': 'Chip select 0 (active low)'},
        {'id': 'cs1', 'name': 'CS1', 'desc': 'Chip select 1 (active low)'},
        ] + [{'id': 'd%d' % i, 'name': 'D%d' % i, 'desc': 'Data line %d' % i} for i in range(16)] + [
        {'id': 'data_sel', 'name': 'DATA_SEL', 'desc': 'DATA_SEL in ZuluIDE hardware'},
        ]
    )
    options = ()
    annotations = (
        ('device', 'Selected device'),
        ('regwr', 'Register writes'),
        ('regrd', 'Register reads'),
        ('datawr', 'Data writes'),
        ('datard', 'Data reads'),
        ('cmd', 'Commands'),
        ('datatransfer', 'Data transfers'),
        ('status', 'Device status'),
        ('atapi', 'ATAPI commands'),
        ('event', 'Other events'),
    )
    annotation_rows = (
        ('device_row', 'Selected device', (Annotations.device,)),
        ('reg_row', 'Register access', (Annotations.regwr, Annotations.regrd, Annotations.datawr, Annotations.datard)),
        ('commands', 'Commands', (Annotations.cmd, Annotations.atapi)),
        ('transfers', 'Data transfer', (Annotations.datatransfer,)),
        ('statuses', 'Device status', (Annotations.status,)),
        ('events', 'Events', (Annotations.event,)),
    )

    def __init__(self):
        self.reset()

    def reset(self):
        self.prev_cmd = None
        self.prev_atapi = None
        self.prev_data_transfer = None
        self.prev_device = None
        self.prev_status = None

    def start(self):
        self.out_ann = self.register(srd.OUTPUT_ANN)

    def get_regaddr(self, pins):
        '''Convert address lines to register address'''
        return bitpack([pins[Pins.DA0], pins[Pins.DA1], pins[Pins.DA2], pins[Pins.CS0], pins[Pins.CS1]])

    def process_states(self, start, end, was_write, addr, data):
        '''Process register read/writes to determine transfer states'''

        is_cmd_write = (was_write and addr == 0x17)
        is_device_change = (was_write and addr == 0x16)
        
        if self.prev_atapi is not None:
            if is_cmd_write or is_device_change:
                cmd_start, cmd = self.prev_atapi
                cmdname = ATAPI_COMMANDS.get(cmd, "(unknown ATAPI cmd)")
                texts = [
                    "ATAPI command 0x%02X: %s" % (cmd, cmdname),
                    "ATAPI %s" % cmdname,
                    "A %02X" % cmd,
                ]
                self.put(cmd_start, start, self.out_ann, [Annotations.atapi, texts])
                self.prev_atapi = None

        if self.prev_cmd is not None:
            cmd_start, cmd = self.prev_cmd
            is_atapi_cmd = (was_write and addr == 0x10 and cmd == 0xA0)
            if is_cmd_write or is_device_change or is_atapi_cmd:
                # New command write or device change
                cmdname = ATA_COMMANDS.get(cmd, "(unknown cmd)")
                texts = [
                    "Command 0x%02X: %s" % (cmd, cmdname),
                    "CMD %s" % cmdname,
                    "C %02X" % cmd,
                ]
                self.put(cmd_start, start, self.out_ann, [Annotations.cmd, texts])
                self.prev_cmd = None
            
            if is_atapi_cmd:
                self.prev_atapi = (start, data & 0xFF)

        if self.prev_data_transfer is not None:
            transfer_start, transfer_write, count = self.prev_data_transfer
            if addr != 0x10 or was_write != transfer_write:
                texts = [
                    "Data %s %d words" % ("write" if transfer_write else "read", count),
                    "D %d" % count,
                ]
                self.put(transfer_start, start, self.out_ann, [Annotations.datatransfer, texts])
                self.prev_data_transfer = None

        is_status_read = (not was_write and addr in [0x0E, 0x17])
        if self.prev_status is not None and (is_status_read or is_device_change or is_cmd_write):
            status_start, status_val = self.prev_status

            if addr not in [0x0E, 0x17] or was_write or data != status_val:
                # New command or device selection, or status changed
                flags = ''
                if status_val & 0x80: flags += 'BSY(7) '
                if status_val & 0x40: flags += 'DRDY(6) '
                if status_val & 0x20: flags += 'DEVFAULT(5) '
                if status_val & 0x10: flags += 'SEEKDONE(4) '
                if status_val & 0x08: flags += 'DATAREQ(3) '
                if status_val & 0x04: flags += 'ERRCORR(2) '
                if status_val & 0x02: flags += 'IDX(1) '
                if status_val & 0x01: flags += 'ERR(0) '
                texts = [
                    'Status 0x%02X %s' % (status_val, flags.strip()),
                    'S %02X' % status_val
                ]
                self.put(status_start, start, self.out_ann, [Annotations.status, texts])
                self.prev_status = None

        if addr == 0x10:
            # Data read / write
            if self.prev_data_transfer is not None:
                self.prev_data_transfer[2] += 1
            else:
                self.prev_data_transfer = [start, was_write, 1]

        elif not was_write and addr in [0x0E, 0x17]:
            # Status register read
            if not self.prev_status:
                self.prev_status = (start, data)

        elif was_write and addr == 0x16:
            # Device selection register
            new_dev = (data >> 4) & 1

            if self.prev_device is not None:
                old_dev, old_start = self.prev_device
                if new_dev != old_dev:
                    self.put(old_start, start, self.out_ann,
                        [Annotations.device, ["D%d" % old_dev, "Device %d" % old_dev]])
                    self.prev_device = None

            if self.prev_device is None:
                self.prev_device = (new_dev, start)

        elif was_write and addr == 0x17:
            # Write of command
            self.prev_cmd = (start, data)

    def process_eof_states(self):
        '''Dump out the end of any current state'''
        # Fake device selection change to end annotations
        if self.prev_device and self.prev_device[0]:
            self.process_states(self.samplenum, self.samplenum, True, 0x16, 0x00)
        else:
            self.process_states(self.samplenum, self.samplenum, True, 0x16, 0x10)

    def handle_write(self, start, end, pins):
        '''Write annotations for a register write'''
        addr = self.get_regaddr(pins)

        if addr == 0x10:
            # Write of data (16 bits)
            data = bitpack(pins[Pins.D0:Pins.D16])
            texts = [
                "Write data 0x%04X" % data,
                "W %04X" % data,
            ]
            self.put(start, end, self.out_ann, [Annotations.datawr, texts])
            self.process_states(start, end, True, addr, data)

        else:
            # Register write (8 bits)
            data = bitpack(pins[Pins.D0:Pins.D8])
            regname = REGADDR_WRITE.get(addr, "UNKNOWN")
            texts = [
                "Write %s (0x%02X): value 0x%02X" % (regname, addr, data),
                "WR %s %02X" % (regname, data),
                "W %02X" % data,
            ]
            self.put(start, end, self.out_ann, [Annotations.regwr, texts])
            self.process_states(start, end, True, addr, data)


    def handle_read(self, start, end, pins):
        '''Write annotations for a register read'''
        addr = self.get_regaddr(pins)

        if addr == 0x10:
            # Read of data (16 bits)
            data = bitpack(pins[Pins.D0:Pins.D16])
            texts = [
                "Read data 0x%04X" % data,
                "R %04x" % data
            ]
            self.put(start, end, self.out_ann, [Annotations.datard, texts])
            self.process_states(start, end, False, addr, data)

        else:
            # Register read (8 bits)
            data = bitpack(pins[Pins.D0:Pins.D8])
            regname = REGADDR_READ.get(addr, "UNKNOWN")
            texts = [
                "Read %s (0x%02X): value 0x%02X" % (regname, addr, data),
                "RD %s %02X" % (regname, data),
                "R %02X" % data
            ]
            self.put(start, end, self.out_ann, [Annotations.regrd, texts])
            self.process_states(start, end, False, addr, data)

    def ctrlmux_text(self, data):
        r = []
        for inv, bit, name in CTRL_MUX_BITS:
            if bool(data & bit) != inv:
                r.append(name)
        return ', '.join(r)

    def decode(self):
        wait_conditions = [{Pins.IOW: 'e'}, {Pins.IOR: 'e'}, {Pins.D15: 'e'}]
        while True:
            try:
                pins = self.wait(wait_conditions)
            except EOFError:
                self.process_eof_states()
                break

            iow = pins[Pins.IOW]
            ior = pins[Pins.IOR]
            data = bitpack(pins[Pins.D0:Pins.D16])

            if not ior and not iow and 1 not in pins[0:Pins.D16]:
                start = self.samplenum

                while not pins[Pins.IOW] and not pins[Pins.IOR]:
                    pins = self.wait(wait_conditions)

                texts = [
                    'Sniffer capture overflow, lost samples',
                    'Capture overflow',
                    'Overflow'
                ]
                self.put(start, self.samplenum, self.out_ann,
                         [Annotations.event, texts,])

            elif pins[Pins.DATA_SEL] and ior and iow and data != 0xFFFF:
                start = self.samplenum

                while not pins[Pins.IOW] and not pins[Pins.IOR]:
                    pins = self.wait(wait_conditions)

                if self.samplenum - start < 5:
                    texts = [
                        'Control mux write 0x%02x' % (data >> 8) + " " + self.ctrlmux_text(data),
                        'Ctrl Mux 0x%02x' % (data >> 8),
                        '%02x' % (data >> 8)
                    ]
                    self.put(start, start + 10, self.out_ann,
                         [Annotations.event, texts,])

            elif not iow:
                start = self.samplenum

                while not pins[Pins.IOW]:
                    prev_pins = pins
                    pins = self.wait()

                self.handle_write(start, self.samplenum, prev_pins)

            elif not ior:
                start = self.samplenum

                while not pins[Pins.IOR]:
                    prev_pins = pins
                    pins = self.wait()

                self.handle_read(start, self.samplenum, prev_pins)


