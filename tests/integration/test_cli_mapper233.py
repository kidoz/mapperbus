"""Original mapper-233 loading smoke; unit/app fixtures assert CPU completion."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


PROFILES = ("reset-42", "disch-233", "everynes-vector")


def rom(profile=0, legacy=False, vertical=True, pages=64, chr_pages=0):
    header = bytearray(b"NES\x1a" + bytes(12))
    header[4], header[5] = pages, chr_pages
    header[6], header[7] = 0x90 | int(vertical), 0xE0 if legacy else 0xE8
    header[11] = 0 if legacy else 7
    code = bytearray([0x78])

    def store(address, value):
        code.extend([0xA9, value, 0x8D, address & 255, address >> 8])

    def expect(address, value):
        code.extend([0xAD, address & 255, address >> 8, 0xC9, value, 0xD0, 0xFE])

    store(0x4017, 0x40)
    for value in (0, 1, 0x3F, 0x55, 0x80, 0xFF):
        store(0x8E8E, value)
        base = 0 if profile == 1 else (value // 128) * 32
        page = value % 32
        low = page if value & 32 else (page // 2) * 2
        high = page if value & 32 else (page // 2) * 2 + 1
        expect(0x9A00, (base + low) % max(pages, 1))
        expect(0xFA00, (base + high) % max(pages, 1))
    for address, value in ((0x8001, 0), (0x2006, 1), (0x2006, 0x23),
                           (0x2007, 0x39), (0x8001, 2), (0x2006, 1),
                           (0x2006, 0x23), (0x2007, 0xA7), (0x8001, 0)):
        store(address, value)
    if profile == 2:
        store(0x8000, 0)
        expect(0xFFFD, 0xE0)
        expect(0x9A00, 63 % max(pages, 1))
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
    with tempfile.TemporaryDirectory(prefix="mapperbus-233-cli-") as temp:
        root = Path(temp)
        env = dict(os.environ, XDG_CONFIG_HOME=str(root / "config"),
                   XDG_DATA_HOME=str(root / "data"))

        def run(source, profile=None):
            args = [str(binary), str(source), "--frames", "2"]
            if profile is not None:
                args += ["--board-profile", profile]
            return subprocess.run(args, env=env, capture_output=True, text=True,
                                  timeout=15, check=False)

        def reject(data, profile=None):
            nonlocal invalid
            source = root / f"invalid-{invalid}.nes"
            source.write_bytes(data)
            result = run(source, profile)
            assert result.returncode != 0, result.stderr
            assert source.read_bytes() == data and not source.with_suffix(".sav").exists()
            invalid += 1

        for profile in range(3):
            for legacy in (False, True):
                for vertical in (False, True):
                    for pages in (1, 3, 32, 64):
                        if profile == 1 and pages > 32:
                            continue
                        source = root / f"233-{profile}-{legacy}-{vertical}-{pages}.nes"
                        data = rom(profile, legacy, vertical, pages)
                        source.write_bytes(data)
                        result = run(source, PROFILES[profile])
                        assert result.returncode == 0 and "Ran 2 frames successfully" in result.stderr, result.stderr
                        assert source.read_bytes() == data and not source.with_suffix(".sav").exists()
                        valid += 1
        for legacy in (False, True):
            for vertical in (False, True):
                source = root / f"default-{legacy}-{vertical}.nes"
                data = rom(legacy=legacy, vertical=vertical)
                source.write_bytes(data)
                result = run(source)
                assert result.returncode == 0, result.stderr
                assert source.read_bytes() == data and not source.with_suffix(".sav").exists()
                valid += 1
                reject(data, "disch-233")
        for offset, value in ((8, 0x10), (8, 0xF0), (6, 0x92), (6, 0x98),
                              (10, 7), (10, 0x70), (11, 0), (11, 6), (11, 8), (11, 0x70)):
            data = rom(pages=1)
            data[offset] = value
            reject(data)
        for legacy in (False, True):
            for pages, chr_pages in ((0, 0), (65, 0), (128, 0), (32, 1)):
                reject(rom(legacy=legacy, pages=pages, chr_pages=chr_pages))
        for flags in (0x92, 0x98):
            data = rom(legacy=True, pages=1)
            data[6] = flags
            reject(data)
        for profile in ("unknown", "disch", "reset"):
            reject(rom(pages=1), profile)
    print(f"Mapper 233: {valid} loading profiles and {invalid} rejected inputs passed")


if __name__ == "__main__":
    main()
