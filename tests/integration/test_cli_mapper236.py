"""Original Realtec loading probes; CPU completion is asserted in core/app tests."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


SIZES = ((8, 8), (16, 16), (32, 0))  # 16 KiB PRG / 8 KiB CHR units.


def rom(population=0, pads=0, legacy=False, vertical=True, pages=None, chr_pages=None):
    if pages is None:
        pages = SIZES[population][0]
    if chr_pages is None:
        chr_pages = SIZES[population][1]
    header = bytearray(b"NES\x1a" + bytes(12))
    header[4], header[5] = pages, chr_pages
    header[6], header[7] = 0xC0 | int(vertical), 0xE0 if legacy else 0xE8
    if not legacy and population == 2:
        header[11] = 7
    code = bytearray([0x78])

    def store(address, value):
        code.extend([0xA9, value, 0x8D, address & 255, address >> 8])

    def expect(address, value):
        code.extend([0xAD, address & 255, address >> 8, 0xC9, value & 255, 0xD0, 0xFE])

    store(0x4017, 0x40)
    store(0x8007 if population == 2 else 0x800F, 0xFF)
    block = 24 if population == 2 else 0
    for mode in (0, 2, 3):
        store(0xC003 + mode * 16, 0)
        low = 2 if mode == 2 else 3
        high = 7 if mode == 0 else 3
        expect(0x9A80, (block + low) * 7)
        expect(0xFA80, (block + high) * 7)
    store(0xC013, 0xFF)
    for nibble in range(16):
        expect(0x9A80 + nibble, (block + 3) * 7 + pads)
        expect(0xFA80 + nibble, (block + 7) * 7 + pads)
    store(0xC033, 0)
    for value in (1, 0x23):
        store(0x2006, value)
    store(0x2007, 0x5A)
    for value in (1, 0x23):
        store(0x2006, value)
    code.extend([0xAD, 7, 0x20])
    expect(0x2007, 0x5A if population == 2 else (7 if population == 0 else 15))
    store(0x12, 0xC7)
    loop = 0x200 + len(code)
    code.extend([0x4C, loop & 255, loop >> 8])
    assert len(code) < 1536
    boot = bytearray()
    for index, value in enumerate(code):
        address = 0x200 + index
        boot.extend([0xA9, value, 0x8D, address & 255, address >> 8])
    boot.extend([0x4C, 0, 2])
    assert len(boot) < 0x1800
    prg = bytearray()
    for page in range(pages):
        data = bytearray([page]) * 16384
        for base in (0x1A80, 0x3A80):
            data[base:base + 16] = bytes((page * 7 + n) & 255 for n in range(16))
        data[0x2000:0x2000 + len(boot)] = boot
        data[-6:] = bytes([0, 0xE0] * 3)
        prg.extend(data)
    chr_rom = bytearray()
    for bank in range(chr_pages):
        chr_rom.extend(bytes([bank]) * 8192)
    return header + prg + chr_rom


def main():
    binary = Path(sys.argv[1]).resolve()
    valid = invalid = 0
    with tempfile.TemporaryDirectory(prefix="mapperbus-236-cli-") as temp:
        root = Path(temp)
        env = dict(os.environ, XDG_CONFIG_HOME=str(root / "config"),
                   XDG_DATA_HOME=str(root / "data"))

        def probe(data, profile=None, accepted=True):
            nonlocal valid, invalid
            source = root / "probe.nes"
            source.write_bytes(data)
            args = [str(binary), str(source), "--frames", "2"]
            if profile is not None:
                args.extend(["--board-profile", profile])
            result = subprocess.run(args, env=env, capture_output=True, text=True,
                                    timeout=15, check=False)
            if accepted:
                assert result.returncode == 0 and "Ran 2 frames successfully" in result.stderr, result.stderr
                valid += 1
            else:
                assert result.returncode != 0, result.stderr
                invalid += 1
            assert source.read_bytes() == data and not source.with_suffix(".sav").exists()

        for population in range(3):
            for pads in range(16):
                probe(rom(population, pads), f"menu-{pads}")
            for legacy in (False, True):
                for vertical in (False, True):
                    probe(rom(population, legacy=legacy, vertical=vertical))
            for offset, value in ((8, 0x10), (8, 0xF0), (6, 0xC2), (6, 0xC8),
                                  (10, 7), (10, 0x70), (11, 6), (11, 8), (11, 0x77),
                                  (11, 0 if population == 2 else 7)):
                data = rom(population)
                data[offset] = value
                probe(data, accepted=False)
            for legacy in (False, True):
                for flags in (0xC2, 0xC8):
                    data = rom(population, legacy=legacy)
                    data[6] = flags
                    probe(data, accepted=False)
        for legacy in (False, True):
            for pages, chr_pages in ((0, 8), (7, 8), (9, 8), (16, 8), (8, 16),
                                     (32, 8), (64, 0), (8, 0), (32, 1)):
                probe(rom(legacy=legacy, pages=pages, chr_pages=chr_pages), accepted=False)
        for profile in ("unknown", "menu-", "menu--1", "menu-+1", "menu-16", "menu-1x", "menu-42949672960"):
            probe(rom(), profile, accepted=False)
    print(f"Mapper 236: {valid} loading profiles and {invalid} rejected inputs passed")


if __name__ == "__main__":
    main()
