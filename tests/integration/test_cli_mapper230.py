"""Original mapper-230 startup smoke; unit/app fixtures assert CPU mode completion."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def rom(legacy=False, vertical=True, pages=40, chr_pages=0):
    header = bytearray(b"NES\x1a" + bytes(12))
    header[4], header[5] = pages, chr_pages
    header[6], header[7] = 0x60 | int(vertical), 0xE0 if legacy else 0xE8
    header[11] = 0 if legacy else 7
    code = bytearray([0x78])

    def store(address, value):
        code.extend([0xA9, value, 0x8D, address & 255, address >> 8])

    def expect(address, value):
        code.extend([0xAD, address & 255, address >> 8, 0xC9, value, 0xD0, 0xFE])

    store(0x4017, 0x40)
    for bank in range(8):
        store(0x8000, bank)
        expect(0x9A00, bank)
        expect(0xFA00, 7)
    for address, value in ((0x2006, 1), (0x2006, 0x23), (0x2007, 0xA7),
                           (0x8000, 0x40), (0x2006, 0x20), (0x2006, 0),
                           (0x2007, 0x39), (0x8000, 0xFF)):
        store(address, value)
    loop = 0xE000 + len(code)
    code.extend([0x4C, loop & 255, loop >> 8])
    prg = bytearray()
    for page in range(pages):
        data = bytearray([page]) * 16384
        data[0x2000:0x2000 + len(code)] = code
        data[-6:] = bytes([0, 0xE0] * 3)
        prg.extend(data)
    return header + prg + bytes(chr_pages * 8192)


def main():
    binary = Path(sys.argv[1]).resolve()
    valid = invalid = 0
    with tempfile.TemporaryDirectory(prefix="mapperbus-230-cli-") as temp:
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
                for inferred_ram in ((0, 2) if legacy else (0,)):
                    source = root / f"230-{legacy}-{vertical}-{inferred_ram}.nes"
                    data = rom(legacy, vertical)
                    data[8] = inferred_ram
                    source.write_bytes(data)
                    result = run(source)
                    assert result.returncode == 0 and "Ran 2 frames successfully" in result.stderr, result.stderr
                    assert source.read_bytes() == data and not source.with_suffix(".sav").exists()
                    valid += 1
        for offset, value in ((8, 0x10), (8, 0xF0), (6, 0x62), (6, 0x68),
                              (10, 7), (10, 0x70), (11, 0), (11, 6), (11, 8), (11, 0x70)):
            data = rom()
            data[offset] = value
            reject(data)
        for legacy in (False, True):
            for pages, chr_pages in ((0, 0), (8, 0), (32, 0), (39, 0), (41, 0), (40, 1)):
                reject(rom(legacy=legacy, pages=pages, chr_pages=chr_pages))
        for flags in (0x62, 0x68):
            data = rom(legacy=True)
            data[6] = flags
            reject(data)
    print(f"Mapper 230: {valid} startup profiles and {invalid} rejected inputs passed")


if __name__ == "__main__":
    main()
