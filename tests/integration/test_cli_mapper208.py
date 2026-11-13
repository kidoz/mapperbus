"""Mapper-208 loading smoke tests; CPU/protection execution is checked by unit fixtures."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def rom(profile=0, legacy=False, vertical=True, p=8, c=32):
    header = bytearray(b"NES\x1a" + bytes(12))
    header[4], header[5] = p, c
    header[6], header[7] = int(vertical), 0xD0 if legacy else 0xD8
    header[8] = 0 if legacy else profile << 4
    prg = bytearray().join(bytes([page & 255]) * 8192 for page in range(p * 2))
    code = bytearray([0x78])

    def store(address, value):
        code.extend([0xA9, value, 0x8D, address & 255, address >> 8])

    store(0x5000, 9)
    if profile == 0:
        resume = 0xE000 + len(code) + 18
        store(0x5800, 0x4C ^ 0x49)
        store(0x5801, (resume & 255) ^ 0x49)
        store(0x5802, (resume >> 8) ^ 0x49)
        code.extend([0x4C, 0, 0x58])
        for value in (0, 1, 0x10, 0x31):
            store(0x4800, value)
            store(0x6800, value)
    else:
        store(0x5800, 0x4C)
        for value in (0, 7, 14, 255):
            store(0x8000, 6)
            store(0x8001, value)
    loop = 0xE000 + len(code)
    code.extend([0x4C, loop & 255, loop >> 8])
    for base in range(0, len(prg) - 32768 + 1, 32768):
        prg[base + 0x6000:base + 0x6000 + len(code)] = code
        prg[base + 32762:base + 32768] = bytes([0, 0xE0] * 3)
    return header + prg + bytearray(c * 8192)


def main():
    binary = Path(sys.argv[1]).resolve()
    valid_count = invalid_count = 0
    with tempfile.TemporaryDirectory(prefix="mapperbus-208-cli-") as temp:
        root = Path(temp)
        env = dict(os.environ, XDG_CONFIG_HOME=str(root / "config"),
                   XDG_DATA_HOME=str(root / "data"))

        def check(data, valid):
            source = root / f"208-{valid}-{valid_count}-{invalid_count}.nes"
            source.write_bytes(data)
            result = subprocess.run([str(binary), str(source), "--frames", "2"],
                                    env=env, capture_output=True, text=True,
                                    timeout=15, check=False)
            if valid:
                assert result.returncode == 0 and "Ran 2 frames successfully" in result.stderr, result.stderr
            else:
                assert result.returncode != 0
            assert source.read_bytes() == data and not source.with_suffix(".sav").exists()

        for profile in (0, 1):
            for legacy in (False, True):
                if profile == 1 and legacy:
                    continue
                for vertical in (False, True):
                    check(rom(profile, legacy, vertical), True)
                    valid_count += 1
        for profile, p, c in ((0, 2, 1), (0, 4, 3), (0, 6, 7), (1, 2, 1),
                              (1, 6, 3), (1, 12, 7), (1, 16, 32), (1, 128, 32)):
            check(rom(profile, p=p, c=c), True)
            valid_count += 1
        for offset, value in ((8, 0x20), (8, 0xF0), (6, 3), (6, 9),
                              (10, 7), (10, 0x70), (11, 7), (11, 0x70)):
            bad = rom()
            bad[offset] = value
            check(bad, False)
            invalid_count += 1
        for profile, p, c in ((0, 1, 32), (0, 10, 32), (0, 3, 32),
                              (0, 8, 33), (0, 8, 0), (1, 130, 32)):
            check(rom(profile, p=p, c=c), False)
            invalid_count += 1
        for flags in (3, 9):
            bad = rom(legacy=True)
            bad[6] = flags
            check(bad, False)
            invalid_count += 1
    print(f"Mapper 208: {valid_count} boot profiles and {invalid_count} rejected headers passed")


if __name__ == "__main__":
    main()
