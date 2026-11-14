"""Original mapper-228 loading smoke; CPU completion is asserted by unit fixtures."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def rom(legacy=False, vertical=True, pages=96, chr_pages=64):
    header = bytearray(b"NES\x1a" + bytes(12))
    header[4], header[5] = pages, chr_pages
    header[6], header[7] = 0x40 | int(vertical), 0xE0 if legacy else 0xE8
    code = bytearray([0x78])

    def store(address, value):
        code.extend([0xA9, value, 0x8D, address & 255, address >> 8])

    store(0x4017, 0x40)
    for address, value in ((0x8000, 0), (0x87E0, 0xA7), (0x8800, 0),
                           (0x9800, 0), (0xBFEF, 0xFF), (0x2006, 0),
                           (0x2006, 0x23), (0x2007, 0xA7), (0x8000, 0)):
        store(address, value)
    if pages == 96:
        # Run the unpopulated chip-select probe from console RAM. Both reads
        # retain the final absolute-address operand byte on the CPU data bus.
        continuation = 0xE000 + len(code) + 138
        routine = bytes([0xA9, 0, 0x8D, 0, 0x90,
                         0xAD, 0, 0x9A, 0xC9, 0x9A, 0xD0, 0xFE,
                         0xAD, 0, 0xFA, 0xC9, 0xFA, 0xD0, 0xFE,
                         0xA9, 0, 0x8D, 0, 0x80, 0x4C,
                         continuation & 255, continuation >> 8])
        for offset, value in enumerate(routine):
            store(0x200 + offset, value)
        code.extend([0x4C, 0, 2])
    loop = 0xE000 + len(code)
    code.extend([0x4C, loop & 255, loop >> 8])
    prg = bytearray()
    for page in range(pages):
        data = bytearray([page]) * 16384
        data[0x2000:0x2000 + len(code)] = code
        data[-6:] = bytes([0, 0xE0] * 3)
        prg.extend(data)
    chr_rom = bytearray()
    for bank in range(chr_pages):
        chr_rom.extend(bytes([bank]) * 8192)
    return header + prg + chr_rom


def main():
    binary = Path(sys.argv[1]).resolve()
    valid = invalid = 0
    with tempfile.TemporaryDirectory(prefix="mapperbus-228-cli-") as temp:
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

        for legacy in (False, True):
            for vertical in (False, True):
                for pages in (1, 3, 32, 96):
                    for chr_pages in (1, 3, 64):
                        source = root / f"228-{legacy}-{vertical}-{pages}-{chr_pages}.nes"
                        data = rom(legacy, vertical, pages, chr_pages)
                        source.write_bytes(data)
                        result = run(source)
                        assert result.returncode == 0 and "Ran 2 frames successfully" in result.stderr, result.stderr
                        assert source.read_bytes() == data and not source.with_suffix(".sav").exists()
                        valid += 1
        for offset, value in ((8, 0x10), (8, 0xF0), (6, 0x42), (6, 0x48),
                              (10, 7), (10, 0x70), (11, 7), (11, 0x70)):
            data = rom(pages=1, chr_pages=1)
            data[offset] = value
            reject(data)
        for legacy in (False, True):
            for pages, chr_pages in ((0, 1), (33, 1), (64, 1), (95, 1), (97, 1),
                                     (128, 1), (1, 0), (1, 65)):
                reject(rom(legacy=legacy, pages=pages, chr_pages=chr_pages))
        for flags in (0x42, 0x48):
            data = rom(legacy=True, pages=1, chr_pages=1)
            data[6] = flags
            reject(data)
    print(f"Mapper 228: {valid} loading profiles and {invalid} rejected inputs passed")


if __name__ == "__main__":
    main()
