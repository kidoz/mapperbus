"""Original mapper-219 loading smoke; CPU completion is asserted by unit firmware."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def rom(legacy=False, vertical=True, prg_pages=32, chr_pages=64):
    header = bytearray(b"NES\x1a" + bytes(12))
    header[4], header[5] = prg_pages, chr_pages
    header[6], header[7] = 0xB0 | int(vertical), 0xD0 if legacy else 0xD8
    code = bytearray([0x78])
    for outer in range(4):
        for address, value in ((0x5002, outer & 1), (0x7003, (outer & 2) << 4),
                               (0x8002, 0x20), (0x8000, 0x26), (0x8001, 0x24),
                               (0x8000, 0x23), (0x8001, 0x18), (0x8000, 0x10),
                               (0x8001, 3), (0x8000, 0x11), (0x8001, 0x85),
                               (0x8002, 0xC6), (0x8001, 5)):
            code.extend([0xA9, value, 0x8D, address & 255, address >> 8])
    loop = 0xE000 + len(code)
    code.extend([0x4C, loop & 255, loop >> 8])
    prg = bytearray()
    for page in range(prg_pages * 2):
        data = bytearray([page]) * 8192
        data[:len(code)] = code
        data[-6:] = bytes([0, 0xE0] * 3)
        prg.extend(data)
    chr_rom = bytearray().join(bytes([page]) * 8192 for page in range(chr_pages))
    return header + prg + chr_rom


def main():
    binary = Path(sys.argv[1]).resolve()
    valid_count = invalid_count = 0
    with tempfile.TemporaryDirectory(prefix="mapperbus-219-cli-") as temp:
        root = Path(temp)
        env = dict(os.environ, XDG_CONFIG_HOME=str(root / "config"),
                   XDG_DATA_HOME=str(root / "data"))

        def run(source):
            return subprocess.run([str(binary), str(source), "--frames", "2"],
                                  env=env, capture_output=True, text=True,
                                  timeout=15, check=False)

        def reject(data):
            nonlocal invalid_count
            source = root / f"invalid-219-{invalid_count}.nes"
            source.write_bytes(data)
            result = run(source)
            assert result.returncode != 0, result.stderr
            assert source.read_bytes() == data and not source.with_suffix(".sav").exists()
            invalid_count += 1

        for legacy in (False, True):
            for vertical in (False, True):
                for p, c in ((2, 1), (6, 3), (32, 64)):
                    source = root / f"219-{legacy}-{vertical}-{p}-{c}.nes"
                    original = rom(legacy, vertical, p, c)
                    source.write_bytes(original)
                    result = run(source)
                    assert result.returncode == 0 and "Ran 2 frames successfully" in result.stderr, result.stderr
                    assert source.read_bytes() == original
                    assert not source.with_suffix(".sav").exists()
                    valid_count += 1
        for offset, value in ((8, 0x10), (8, 0xF0), (6, 0xB3), (6, 0xB9),
                              (10, 7), (10, 0x70), (11, 7), (11, 0x70)):
            data = rom(prg_pages=2, chr_pages=1)
            data[offset] = value
            reject(data)
        for legacy in (False, True):
            for p, c in ((1, 1), (33, 64), (32, 65), (32, 0)):
                reject(rom(legacy, prg_pages=p, chr_pages=c))
        for flags in (0xB3, 0xB9):
            data = rom(legacy=True, prg_pages=2, chr_pages=1)
            data[6] = flags
            reject(data)
    print(f"Mapper 219: {valid_count} loading profiles and {invalid_count} rejected headers passed")


if __name__ == "__main__":
    main()
