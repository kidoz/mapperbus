"""Mapper-218 loading smoke; original unit firmware verifies CPU/PPU behavior."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def rom(wiring=0, legacy=False, size=32768):
    header = bytearray(b"NES\x1a" + bytes(12))
    header[6], header[7] = 0xA0 | wiring, 0xD0 if legacy else 0xD8
    if size % 16384 == 0:
        header[4] = size // 16384
    else:
        assert not legacy and size & (size - 1) == 0
        header[4], header[9] = (size.bit_length() - 1) << 2, 0x0F
    code = bytearray([0x78])
    for address, value in ((0x2006, 0), (0x2006, 0x23), (0x2007, 0xA7),
                           (0x2006, 0x20), (0x2006, 0x23), (0x2007, 0x5A)):
        code.extend([0xA9, value, 0x8D, address & 255, address >> 8])
    loop = 0xE000 + len(code)
    code.extend([0x4C, loop & 255, loop >> 8])
    prg = bytearray([0xEA]) * size
    offset = 0x6000 % size
    prg[offset:offset + len(code)] = code
    prg[-6:] = bytes([0, 0xE0] * 3)
    return header + prg


def main():
    binary = Path(sys.argv[1]).resolve()
    valid_count = invalid_count = 0
    with tempfile.TemporaryDirectory(prefix="mapperbus-218-cli-") as temp:
        root = Path(temp)
        env = dict(os.environ, XDG_CONFIG_HOME=str(root / "config"),
                   XDG_DATA_HOME=str(root / "data"))

        def run(source):
            return subprocess.run([str(binary), str(source), "--frames", "2"],
                                  env=env, capture_output=True, text=True,
                                  timeout=15, check=False)

        def reject(data):
            nonlocal invalid_count
            source = root / f"invalid-218-{invalid_count}.nes"
            source.write_bytes(data)
            result = run(source)
            assert result.returncode != 0, result.stderr
            assert source.read_bytes() == data and not source.with_suffix(".sav").exists()
            invalid_count += 1

        for legacy in (False, True):
            for wiring in (0, 1, 8, 9):
                for size in ((4096, 8192, 16384, 32768) if not legacy else (16384, 32768)):
                    data = rom(wiring, legacy, size)
                    source = root / f"218-{legacy}-{wiring}-{size}.nes"
                    source.write_bytes(data)
                    result = run(source)
                    assert result.returncode == 0 and "Ran 2 frames successfully" in result.stderr, result.stderr
                    assert source.read_bytes() == data and not source.with_suffix(".sav").exists()
                    valid_count += 1
        for wiring in (0, 1, 8, 9):
            for offset, value in ((8, 0x10), (8, 0xF0), (10, 7), (10, 0x70),
                                  (11, 7), (11, 0x70), (6, 0xA2 | wiring)):
                data = rom(wiring)
                data[offset] = value
                reject(data)
            for legacy in (False, True):
                data = rom(wiring, legacy)
                data[4] = 4
                data.extend(bytes(32768))
                reject(data)
                data = rom(wiring, legacy)
                data[5] = 1
                data.extend(bytes(8192))
                reject(data)
        # NES 2.0 exponent/multiplier encoding of 24 KiB: 2^13 * 3.
        data = rom()
        data[4], data[9] = (13 << 2) | 1, 0x0F
        reject(data[:16 + 24576])
    print(f"Mapper 218: {valid_count} loading profiles and {invalid_count} rejected headers passed")


if __name__ == "__main__":
    main()
