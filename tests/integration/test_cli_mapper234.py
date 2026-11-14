"""Original Maxi 15 loading probes; CPU completion is asserted in unit/app fixtures."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


POPULATIONS = ((32, 64), (64, 64), (64, 128))  # 16 KiB PRG / 8 KiB CHR units.


def rom(population=0, nina=False, write_outer=False, floating=False, legacy=False,
        vertical=True, prg_pages=None, chr_pages=None):
    if prg_pages is None:
        prg_pages = POPULATIONS[population][0]
    if chr_pages is None:
        chr_pages = POPULATIONS[population][1]
    outer = 0x30 if floating else ((0xC7 if nina else 0x0B) | (0x20 if population else 0))
    header = bytearray(b"NES\x1a" + bytes(12))
    header[4], header[5] = prg_pages, chr_pages
    header[6], header[7] = 0xA0 | int(vertical), 0xE0 if legacy else 0xE8
    code = bytearray([0x78])

    def store(address, value):
        code.extend([0xA9, value, 0x8D, address & 255, address >> 8])

    def expect(address, value):
        code.extend([0xAD, address & 255, address >> 8, 0xC9, value, 0xD0, 0xFE])

    store(0x4017, 0x40)
    expect(0xFF80, 0x80)
    expect(0xFF81, 0x40)
    expect(0xFF82, 0)
    expect(0xFFC0, 3)
    store(0xFFDF, 2)
    if write_outer:
        store(0xFF9F, outer)
    else:
        expect(0xFF88, outer)
    if floating:
        expect(0x9A00, 0x9A)
        expect(0xFA00, 0xFA)
        store(0xFFF7, 0x71)  # No conflicting ROM driver remains.
        expect(0xFFF0, 0xFF)
    else:
        for index in range(16):
            expect(0xFFE8 + index, (index // 2) * 16 + index % 2)
            block = outer & 15
            bank = ((block & 14) | (index % 2)) if nina else block
            bank += 16 if population else 0
            expect(0x9A00, bank)
    store(0x12, 0xD7 if floating else 0xC7)
    loop = (0x400 if floating else 0xE000) + len(code)
    code.extend([0x4C, loop & 255, loop >> 8])
    if floating:
        boot = bytearray()
        for index, value in enumerate(code):
            address = 0x400 + index
            boot.extend([0xA9, value, 0x8D, address & 255, address >> 8])
        boot.extend([0x4C, 0, 4])
    else:
        boot = code
    prg = bytearray()
    for page in range(prg_pages // 2):
        data = bytearray([page]) * 32768
        data[0x6000:0x6000 + len(boot)] = boot
        data[0x7F80:0x7F83] = bytes([0x80, 0x40, 0])
        data[0x7F88], data[0x7F9F] = outer, 0xFF
        data[0x7FC0], data[0x7FDF] = 3, 0xFF
        for index in range(16):
            data[0x7FE8 + index] = (index // 2) * 16 + index % 2
        data[-6:] = bytes([0, 0xE0] * 3)
        prg.extend(data)
    if prg_pages % 2:
        prg.extend(bytes(16384))
    chr_rom = bytearray()
    for bank in range(chr_pages):
        chr_rom.extend(bytes([bank]) * 8192)
    return header + prg + chr_rom


def main():
    binary = Path(sys.argv[1]).resolve()
    valid = invalid = 0
    with tempfile.TemporaryDirectory(prefix="mapperbus-234-cli-") as temp:
        root = Path(temp)
        env = dict(os.environ, XDG_CONFIG_HOME=str(root / "config"),
                   XDG_DATA_HOME=str(root / "data"))

        def run(source):
            return subprocess.run([str(binary), str(source), "--frames", "2"],
                                  env=env, capture_output=True, text=True,
                                  timeout=15, check=False)

        def reject(data):
            nonlocal invalid
            source = root / f"invalid-{invalid}.nes"
            source.write_bytes(data)
            result = run(source)
            assert result.returncode != 0, result.stderr
            assert source.read_bytes() == data and not source.with_suffix(".sav").exists()
            invalid += 1

        for population in range(3):
            for legacy in (False, True):
                for vertical in (False, True):
                    for nina in (False, True):
                        for write_outer in (False, True):
                            source = root / f"234-{valid}.nes"
                            data = rom(population, nina, write_outer, False, legacy, vertical)
                            source.write_bytes(data)
                            result = run(source)
                            assert result.returncode == 0 and "Ran 2 frames successfully" in result.stderr, result.stderr
                            assert source.read_bytes() == data and not source.with_suffix(".sav").exists()
                            valid += 1
                source = root / f"234-floating-{population}-{legacy}.nes"
                data = rom(population, floating=True, legacy=legacy)
                source.write_bytes(data)
                result = run(source)
                assert result.returncode == 0, result.stderr
                assert source.read_bytes() == data and not source.with_suffix(".sav").exists()
                valid += 1
        for offset, value in ((8, 0x10), (8, 0xF0), (6, 0xA2), (6, 0xA8),
                              (10, 7), (10, 0x70), (11, 7), (11, 0x70)):
            data = rom()
            data[offset] = value
            reject(data)
        for legacy in (False, True):
            for prg_pages, chr_pages in ((0, 64), (16, 64), (36, 64), (33, 64),
                                         (96, 64), (32, 0), (32, 32), (32, 128), (64, 192)):
                reject(rom(legacy=legacy, prg_pages=prg_pages, chr_pages=chr_pages))
        for flags in (0xA2, 0xA8):
            data = rom(legacy=True)
            data[6] = flags
            reject(data)
    print(f"Mapper 234: {valid} loading profiles and {invalid} rejected inputs passed")


if __name__ == "__main__":
    main()
