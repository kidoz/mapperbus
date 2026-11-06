#!/usr/bin/env python3
"""Build an original, read-only FDS audio capture program; no assembler or BIOS needed."""

import argparse
import hashlib
import json
from pathlib import Path
import struct


class Code:
    """Small byte emitter with checked 6502 address/branch relocations."""

    def __init__(self, origin=0x6000):
        self.origin = origin
        self.data = bytearray()
        self.labels = {}
        self.fixups = []

    def emit(self, *values):
        self.data.extend(values)

    def label(self, name):
        if name in self.labels:
            raise ValueError(f'duplicate label: {name}')
        self.labels[name] = self.origin + len(self.data)

    def word(self, opcode, address):
        self.emit(opcode)
        if isinstance(address, str):
            self.fixups.append((len(self.data), address, False))
            self.emit(0, 0)
        else:
            self.data.extend(struct.pack('<H', address))

    def branch(self, opcode, label):
        self.emit(opcode)
        self.fixups.append((len(self.data), label, True))
        self.emit(0)

    def store(self, address, value):
        self.emit(0xA9, value)  # LDA #imm
        self.word(0x8D, address)

    def delay(self, cycles):
        if cycles < 0 or cycles == 1:
            raise ValueError('6502 padding cannot take one clock')
        if cycles & 1:
            self.emit(0x24, 0x10)  # BIT zp: three clocks, no register changes
            cycles -= 3
        self.data.extend([0xEA] * (cycles // 2))  # NOP: two clocks

    def finish(self):
        for offset, label, relative in self.fixups:
            address = self.labels[label]
            if relative:
                delta = address - (self.origin + offset + 1)
                if not -128 <= delta <= 127:
                    raise ValueError(f'branch to {label} out of range')
                self.data[offset] = delta & 255
            else:
                struct.pack_into('<H', self.data, offset, address)
        if self.origin + len(self.data) > 0xD000:
            raise ValueError('probe code overlaps result RAM')
        return bytes(self.data)


# Original five-by-seven glyphs. Tile zero is blank; all glyphs use plane zero.
GLYPHS = {
    '0': [14, 17, 19, 21, 25, 17, 14], '1': [4, 12, 4, 4, 4, 4, 14],
    '2': [14, 17, 1, 2, 4, 8, 31], '3': [30, 1, 1, 14, 1, 1, 30],
    '4': [2, 6, 10, 18, 31, 2, 2], '5': [31, 16, 16, 30, 1, 1, 30],
    '6': [14, 16, 16, 30, 17, 17, 14], '7': [31, 1, 2, 4, 8, 8, 8],
    '8': [14, 17, 17, 14, 17, 17, 14], '9': [14, 17, 17, 15, 1, 1, 14],
    'A': [14, 17, 17, 31, 17, 17, 17], 'B': [30, 17, 17, 30, 17, 17, 30],
    'C': [14, 17, 16, 16, 16, 17, 14], 'D': [30, 17, 17, 17, 17, 17, 30],
    'E': [31, 16, 16, 30, 16, 16, 31], 'F': [31, 16, 16, 30, 16, 16, 16],
    'G': [14, 17, 16, 23, 17, 17, 15], 'H': [17, 17, 17, 31, 17, 17, 17],
    'I': [14, 4, 4, 4, 4, 4, 14], 'J': [7, 2, 2, 2, 18, 18, 12],
    'K': [17, 18, 20, 24, 20, 18, 17], 'L': [16, 16, 16, 16, 16, 16, 31],
    'M': [17, 27, 21, 21, 17, 17, 17], 'N': [17, 25, 21, 19, 17, 17, 17],
    'O': [14, 17, 17, 17, 17, 17, 14], 'P': [30, 17, 17, 30, 16, 16, 16],
    'Q': [14, 17, 17, 17, 21, 18, 13], 'R': [30, 17, 17, 30, 20, 18, 17],
    'S': [15, 16, 16, 14, 1, 1, 30], 'T': [31, 4, 4, 4, 4, 4, 4],
    'U': [17, 17, 17, 17, 17, 17, 14], 'V': [17, 17, 17, 17, 17, 10, 4],
    'W': [17, 17, 17, 21, 21, 21, 10], 'X': [17, 17, 10, 4, 10, 17, 17],
    'Y': [17, 17, 10, 4, 4, 4, 4], 'Z': [31, 1, 2, 4, 8, 16, 31],
    '/': [1, 2, 2, 4, 8, 8, 16],
}
TILES = {' ': 0, **{char: i + 1 for i, char in enumerate(GLYPHS)}}


def build():
    c = Code()
    pages = []
    for counter, gain in [(1, 63), (50, 62), (127, 63), (64, 63)]:
        pages.append(dict(kind='product', counter=counter, gain=gain, first=0x4094, second=0x4095))
    for entry in [1, 7, 4]:
        pages.append(dict(kind='increment', entry=entry, first=0x4095, second=0x4097))
    for kind in ['reset', 'reset-held', 'resume']:
        pages.append(dict(kind=kind, entry=7, first=0x4095, second=0x4097))
    for master in [0, 1, 255]:
        for halted in [False, True]:
            pages.append(dict(kind='envelope', master=master, halted=halted,
                              first=0x4090, second=0x4092))
    assert len(pages) == 16

    c.label('entry')
    c.emit(0x78, 0xD8, 0xA2, 0xFF, 0x9A)  # SEI, CLD, LDX #FF, TXS
    for address, value in [(0x2000, 0), (0x2001, 0), (0x4022, 0), (0x4015, 0),
                           (0x4017, 0x40), (0x4023, 3), (0x4025, 0x26), (0x0102, 0x35), (0x0103, 0xAC)]:
        c.store(address, value)
    c.word(0x20, 'vblank')
    c.word(0x20, 'vblank')
    c.word(0x2C, 0x2002)
    c.store(0x2006, 0)
    c.store(0x2006, 0)
    for offset in range(0, 1024, 256):
        c.emit(0xA2, 0)
        c.label(f'font_loop{offset}')
        c.word(0xBD, f'font{offset}')
        c.word(0x8D, 0x2007)
        c.emit(0xE8)
        c.branch(0xD0, f'font_loop{offset}')
    c.store(0x14, 0)  # page
    c.store(0x15, 0)  # previous controller state
    c.label('capture')
    c.store(0x05FF, 0)  # completion marker; result ABI v1
    for page, spec in enumerate(pages):
        for phase in range(32):
            c.word(0x20, f'setup{page}')  # returns A = trigger value
            trigger = 0x4085 if spec['kind'] == 'product' else 0x4083
            if spec['kind'] in ['reset', 'reset-held', 'resume']:
                trigger = 0x4023
            c.word(0x8D, trigger)
            c.delay(phase + 2)  # all 32 residues; read occurs 6..37 clocks after write
            c.word(0xAD, spec['first'])
            c.word(0x8D, 0xD000 + page * 64 + phase)
            c.word(0xAD, spec['second'])
            c.word(0x8D, 0xD000 + page * 64 + 32 + phase)
    c.store(0x05FF, 0xA5)
    c.word(0x20, 'draw')
    c.label('idle')
    c.word(0x20, 'vblank')
    c.store(0x4016, 1)
    c.store(0x4016, 0)
    c.store(0x16, 0)
    c.emit(0xA2, 8)
    c.label('pad')
    c.word(0xAD, 0x4016)
    c.emit(0x4A, 0x26, 0x16, 0xCA)  # LSR A, ROL zp, DEX
    c.branch(0xD0, 'pad')
    c.emit(0xA5, 0x15, 0x49, 0xFF, 0x25, 0x16, 0x85, 0x17)
    c.emit(0xA5, 0x16, 0x85, 0x15, 0xA5, 0x17, 0x29, 0x80)
    c.branch(0xF0, 'check_page')
    c.word(0x4C, 'capture')
    c.label('check_page')
    c.emit(0xA5, 0x17, 0x29, 3)
    c.branch(0xF0, 'idle')
    c.emit(0x29, 1)
    c.branch(0xF0, 'left')
    c.emit(0xE6, 0x14)  # INC page
    c.word(0x4C, 'page_changed')
    c.label('left')
    c.emit(0xC6, 0x14)
    c.label('page_changed')
    c.emit(0xA5, 0x14, 0x29, 15, 0x85, 0x14)
    c.word(0x20, 'draw')
    c.word(0x4C, 'idle')

    for page, spec in enumerate(pages):
        c.label(f'setup{page}')
        # Keep output muted and watchdog IRQ off. All capture code is read-only.
        for address, value in [(0x4023, 3), (0x4080, 0x80), (0x4083, 0xC0),
                               (0x4082, 1), (0x4087, 0x80), (0x4086, 0),
                               (0x4084, 0x80 | spec.get('gain', 63)), (0x4085, 0)]:
            c.store(address, value)
        c.emit(0xA9, spec.get('entry', 1), 0xA2, 32)
        c.label(f'table{page}')
        c.word(0x8D, 0x4088)
        c.emit(0xCA)
        c.branch(0xD0, f'table{page}')
        kind = spec['kind']
        if kind == 'envelope':
            c.store(0x408A, spec['master'])
            c.store(0x4080, 0xA0)  # seed gains while both timers are held
            c.store(0x4084, 0x90)
            c.store(0x4080, 0)
            c.store(0x4084, 0)
            c.emit(0xA9, 0x80 if spec['halted'] else 0)
        elif kind == 'product':
            c.store(0x4083, 0x40)  # run wave, hold envelopes
            c.emit(0xA9, spec['counter'])
        elif kind == 'increment':
            c.store(0x4087, 0x40)  # force carry once per unit tick
            c.emit(0xA9, 0x40)  # release wave halt
        elif kind == 'reset':
            c.store(0x4083, 0x40)
            c.emit(0xA9, 1)  # assert sound reset
        else:
            c.store(0x4023, 1)
            # Hold reset ~74k clocks. Absolute reads outside PRG RAM also
            # refresh real adapter DRAM during this wait.
            c.emit(0xA2, 0, 0xA0, 32)
            c.label(f'hold{page}')
            c.word(0x2C, 0x4032)
            c.emit(0xCA)
            c.branch(0xD0, f'hold{page}')
            c.emit(0x88)
            c.branch(0xD0, f'hold{page}')
            c.emit(0xA9, 3 if kind == 'resume' else 1)
        c.emit(0x60)

    c.label('vblank')
    c.word(0x2C, 0x2002)
    c.branch(0x10, 'vblank')
    c.emit(0x60)

    def ppu_address(address):
        c.word(0x2C, 0x2002)
        c.store(0x2006, address >> 8)
        c.store(0x2006, address & 255)

    def text(row, col, message):
        ppu_address(0x2000 + row * 32 + col)
        for char in message:
            c.store(0x2007, TILES[char])

    c.label('draw')
    c.store(0x2001, 0)
    ppu_address(0x2000)
    c.emit(0xA9, 0, 0xA2, 0, 0xA0, 4)
    c.label('clear')
    c.word(0x8D, 0x2007)
    c.emit(0xCA)
    c.branch(0xD0, 'clear')
    c.emit(0x88)
    c.branch(0xD0, 'clear')
    ppu_address(0x3F00)
    for value in [0x0F, 0x30, 0x30, 0x30]:
        c.store(0x2007, value)
    for row, message in [(2, 'FDS AUDIO PROBE'), (3, 'PAGE'),
                         (5, 'PADDING 02 TO 33 CPU CLOCKS'), (7, 'FIRST REGISTER'),
                         (13, 'SECOND REGISTER'), (19, 'LEFT RIGHT PAGE'),
                         (20, 'A REPEATS ALL'), (22, 'CAPTURE NOT PASS/FAIL')]:
        text(row, 2, message)
    ppu_address(0x2067)
    c.emit(0xA5, 0x14)
    c.word(0x20, 'hex')
    for row, field in [(7, 'first'), (13, 'second')]:
        ppu_address(0x2000 + row * 32 + 18)
        c.emit(0xA6, 0x14)
        c.word(0xBD, field + '_hi')
        c.word(0x20, 'hex')
        c.word(0xBD, field + '_lo')
        c.word(0x20, 'hex')
    # Result pointer = $D000 + page * 64.
    c.emit(0xA5, 0x14, 0x4A, 0x4A, 0x18, 0x69, 0xD0, 0x85, 0x21)
    c.emit(0xA5, 0x14, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x85, 0x20)
    c.emit(0xA0, 0)
    for row in [8, 9, 10, 11, 14, 15, 16, 17]:
        ppu_address(0x2000 + row * 32 + 3)
        c.emit(0xA2, 8)
        label = f'row{row}'
        c.label(label)
        c.emit(0xB1, 0x20)  # LDA (result pointer),Y
        c.word(0x20, 'hex')
        c.store(0x2007, 0)
        c.emit(0xC8, 0xCA)
        c.branch(0xD0, label)
    c.store(0x2000, 0)
    c.store(0x2005, 0)
    c.store(0x2005, 0)
    c.word(0x20, 'vblank')
    c.store(0x2001, 0x0A)
    c.emit(0x60)
    c.label('hex')
    c.emit(0x48, 0x4A, 0x4A, 0x4A, 0x4A, 0x18, 0x69, 1)
    c.word(0x8D, 0x2007)
    c.emit(0x68, 0x29, 15, 0x18, 0x69, 1)
    c.word(0x8D, 0x2007)
    c.emit(0x60)

    font = bytearray(16)
    for rows in GLYPHS.values():
        font.extend(bytes([0] + [row << 2 for row in rows]) + bytes(8))
    font.extend(bytes(1024 - len(font)))
    for offset in range(0, 1024, 256):
        c.label(f'font{offset}')
        c.data.extend(font[offset:offset + 256])
    for field in ['first', 'second']:
        c.label(field + '_hi')
        c.data.extend(spec[field] >> 8 for spec in pages)
        c.label(field + '_lo')
        c.data.extend(spec[field] & 255 for spec in pages)
    program = c.finish()
    vectors = struct.pack('<5H', *([0x6000] * 5))
    files = [(b'AUDIOPRB', 0x6000, program), (b'VECTORS-', 0xDFF6, vectors),
             (b'STARTNMI', 0x2000, bytes([0x80])),
             # Real non-boot file keeps the BIOS busy until the NMI boot entry.
             (b'WAITNMI-', 0xB000, bytes(4096))]
    disk = bytearray(56)
    disk[0] = 1
    disk[1:15] = b'*NINTENDO-HVC*'
    disk[16:20] = b'APR '
    disk[25] = 2  # boot ID: all three boot files
    disk.extend([2, len(files)])
    for number, (name, address, payload) in enumerate(files):
        disk.extend(bytes([3, number, number]) + name + struct.pack('<HHB', address, len(payload), 0))
        disk.append(4)
        disk.extend(payload)
    if len(disk) > 65500:
        raise ValueError('probe does not fit one side')
    disk.extend(bytes(65500 - len(disk)))
    manifest = dict(format='mapperbus-fds-audio-probe', version=1,
                    sha256=hashlib.sha256(disk).hexdigest(), program_bytes=len(program),
                    entry=0x6000, idle=c.labels['idle'], result_base=0xD000,
                    completion_address=0x05FF, completion_value=0xA5,
                    padding_clocks=list(range(2, 34)),
                    first_read_after_trigger=list(range(6, 38)),
                    second_read_after_trigger=list(range(14, 46)), pages=pages,
                    timing='Hardware bus cycles; instruction-stepped emulators approximate access times.',
                    provenance='Original capture software; no physical hardware observations are bundled.')
    return bytes(disk), manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--manifest', type=Path, required=True)
    args = parser.parse_args()
    disk, manifest = build()
    for path in [args.output, args.manifest]:
        path.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(disk)
    args.manifest.write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    print(f'Built {args.output}: {manifest["program_bytes"]} program bytes, 16 capture pages')


if __name__ == '__main__':
    main()
