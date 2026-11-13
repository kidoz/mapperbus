"""Original mapper-215 loading smoke; CPU completion is checked by unit fixtures."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def rom(profile=0, legacy=False, vertical=True, prg_pages=64, chr_pages=128):
    header = bytearray(b"NES\x1a" + bytes(12))
    header[4], header[5] = prg_pages & 255, chr_pages & 255
    header[6], header[7] = 0x70 | int(vertical), 0xD0 if legacy else 0xD8
    if not legacy:
        header[8] = profile << 4
        header[9] = (prg_pages >> 8) | ((chr_pages >> 8) << 4)
    code = bytearray([0x78])
    for pattern in range(8):
        for address, value in ((0x5007, pattern), (0x5001, 0x30 | pattern),
                               (0x5000, 0x8B), (0x5000, 0xEF), (0x5000, 0)):
            code.extend([0xA9, value, 0x8D, address & 255, address >> 8])
    loop = 0xE000 + len(code)
    code.extend([0x4C, loop & 255, loop >> 8])
    prg = bytearray()
    for page in range(prg_pages * 2):
        data = bytearray([page & 255]) * 8192
        data[:len(code)] = code
        data[-6:] = bytes([0, 0xE0] * 3)
        prg.extend(data)
    chr_rom = bytearray().join(bytes([page & 255]) * 8192 for page in range(chr_pages))
    return header + prg + chr_rom


def main():
    binary = Path(sys.argv[1]).resolve()
    valid_count = invalid_count = 0
    with tempfile.TemporaryDirectory(prefix="mapperbus-215-cli-") as temp:
        root = Path(temp)
        env = dict(os.environ, XDG_CONFIG_HOME=str(root / "config"),
                   XDG_DATA_HOME=str(root / "data"))

        def run(source):
            return subprocess.run([str(binary), str(source), "--frames", "2"],
                                  env=env, capture_output=True, text=True,
                                  timeout=15, check=False)

        def reject(data):
            nonlocal invalid_count
            source = root / f"invalid-215-{invalid_count}.nes"
            source.write_bytes(data)
            result = run(source)
            assert result.returncode != 0, result.stderr
            assert source.read_bytes() == data and not source.with_suffix(".sav").exists()
            invalid_count += 1

        for profile, legacy in ((0, False), (1, False), (0, True)):
            maximum = 64 if profile == 0 else 128
            for vertical in (False, True):
                for p, c in ((2, 1), (6, 3), (maximum, maximum * 2)):
                    source = root / f"215-{profile}-{legacy}-{vertical}-{p}-{c}.nes"
                    original = rom(profile, legacy, vertical, p, c)
                    source.write_bytes(original)
                    result = run(source)
                    assert result.returncode == 0 and "Ran 2 frames successfully" in result.stderr, result.stderr
                    assert source.read_bytes() == original
                    assert not source.with_suffix(".sav").exists()
                    valid_count += 1
        for offset, value in ((8, 0x20), (8, 0xF0), (6, 0x73), (6, 0x79),
                              (10, 7), (10, 0x70), (11, 7), (11, 0x70)):
            bad = rom(prg_pages=2, chr_pages=1)
            bad[offset] = value
            reject(bad)
        for profile, legacy in ((0, False), (1, False), (0, True)):
            maximum = 64 if profile == 0 else 128
            for p, c in ((1, 1), (maximum + 1, 1), (2, maximum * 2 + 1), (2, 0)):
                reject(rom(profile, legacy, prg_pages=p, chr_pages=c))
        for value in (0x73, 0x79):
            bad = rom(legacy=True, prg_pages=2, chr_pages=1)
            bad[6] = value
            reject(bad)
    print(f"Mapper 215: {valid_count} loading profiles and {invalid_count} rejected headers passed")


if __name__ == "__main__":
    main()
