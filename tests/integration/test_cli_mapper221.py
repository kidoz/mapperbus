"""Original mapper-221 loading smoke; CPU completion is checked by unit fixtures."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def rom(legacy=False, vertical=True, prg_pages=128, chr_pages=0):
    header = bytearray(b"NES\x1a" + bytes(12))
    header[4], header[5] = prg_pages, chr_pages
    header[6], header[7] = 0xD0 | int(vertical), 0xD0 if legacy else 0xD8
    header[11] = 0 if legacy else 7
    code = bytearray([0x78])
    for address, value in ((0xC005, 0xA7), (0x8000, 0xFF), (0x8002, 0),
                           (0x8102, 0), (0x831F, 0x5A), (0xC00D, 0),
                           (0x2006, 0), (0x2006, 0x23), (0x2007, 0xA7),
                           (0xC005, 0xFF), (0x2006, 0), (0x2006, 0x23),
                           (0x2007, 0x39)):
        code.extend([0xA9, value, 0x8D, address & 255, address >> 8])
    loop = 0xE000 + len(code)
    code.extend([0x4C, loop & 255, loop >> 8])
    prg = bytearray()
    for page in range(prg_pages):
        data = bytearray([page]) * 16384
        data[0x2000:0x2000 + len(code)] = code
        data[-6:] = bytes([0, 0xE0] * 3)
        prg.extend(data)
    return header + prg + bytes(chr_pages * 8192)


def main():
    binary = Path(sys.argv[1]).resolve()
    valid_count = invalid_count = 0
    with tempfile.TemporaryDirectory(prefix="mapperbus-221-cli-") as temp:
        root = Path(temp)
        env = dict(os.environ, XDG_CONFIG_HOME=str(root / "config"),
                   XDG_DATA_HOME=str(root / "data"))

        def run(source):
            return subprocess.run([str(binary), str(source), "--frames", "2"],
                                  env=env, capture_output=True, text=True,
                                  timeout=15, check=False)

        def reject(data):
            nonlocal invalid_count
            source = root / f"invalid-221-{invalid_count}.nes"
            source.write_bytes(data)
            result = run(source)
            assert result.returncode != 0, result.stderr
            assert source.read_bytes() == data and not source.with_suffix(".sav").exists()
            invalid_count += 1

        for legacy in (False, True):
            for vertical in (False, True):
                for pages in (1, 3, 128):
                    source = root / f"221-{legacy}-{vertical}-{pages}.nes"
                    original = rom(legacy, vertical, pages)
                    source.write_bytes(original)
                    result = run(source)
                    assert result.returncode == 0 and "Ran 2 frames successfully" in result.stderr, result.stderr
                    assert source.read_bytes() == original
                    assert not source.with_suffix(".sav").exists()
                    valid_count += 1
        for offset, value in ((8, 0x10), (8, 0xF0), (6, 0xD3), (6, 0xD9),
                              (10, 7), (10, 0x70), (11, 0), (11, 6), (11, 8), (11, 0x70)):
            data = rom(prg_pages=1)
            data[offset] = value
            reject(data)
        for legacy in (False, True):
            for pages, chr_pages in ((0, 0), (129, 0), (2, 1)):
                reject(rom(legacy, prg_pages=pages, chr_pages=chr_pages))
        for flags in (0xD3, 0xD9):
            data = rom(legacy=True, prg_pages=1)
            data[6] = flags
            reject(data)
    print(f"Mapper 221: {valid_count} loading profiles and {invalid_count} rejected headers passed")


if __name__ == "__main__":
    main()
