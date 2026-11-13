"""Original mapper-227 loading probes and observable battery-backed CPU firmware."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def rom(sub=0, battery=False, legacy=False, pages=64, chr_pages=0):
    header = bytearray(b"NES\x1a" + bytes(12))
    header[4], header[5] = pages, chr_pages
    header[6], header[7] = 0x30 | (2 if battery else 0), 0xE0 if legacy else 0xE8
    header[8] = 0 if legacy else sub << 4
    header[10] = 0x70 if battery and not legacy else 0
    header[11] = 0 if legacy else 7
    code = bytearray([0x78])

    def store(address, value):
        code.extend([0xA9, value, 0x8D, address & 255, address >> 8])

    def expect(address, value):
        code.extend([0xAD, address & 255, address >> 8, 0xC9, value, 0xD0, 0xFE])

    store(0x4017, 0x40)
    for block in (0, 3, 7):
        for inner in (0, 5):
            for mode in (0, 1, 128, 129, 512, 513):
                address = (0x8000 + (block // 4) * 256 + (block % 4) * 8
                           + (inner // 2) * 32 + (inner % 2) * 4 + mode)
                store(address, 0xA7)
                low = inner & 6 if mode & 1 else inner
                high = ((low + 1 if mode & 1 else inner) if battery or mode & 128
                        else (7 if mode & 512 else 0))
                low += (block & 4) * 8 if sub == 2 and low == 0 else block * 8
                high += (block & 4) * 8 if sub == 2 and high == 0 else block * 8
                expect(0x9A00, low % max(pages, 1))
                expect(0xFA00, high % max(pages, 1))
    store(0x8000, 0)
    if battery:
        store(0x6123, 0x5A)
        expect(0x6123, 0x5A)
        code.extend([0xEE, 0xFF, 0x7F])  # Increment persistent boot count.
        store(0x6000, 0xC7)  # Reached only after every PRG assertion.
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
    valid = invalid = persistent = 0
    with tempfile.TemporaryDirectory(prefix="mapperbus-227-cli-") as temp:
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

        for sub, battery, legacy in ((0, False, False), (1, False, False), (2, False, False),
                                     (0, True, False), (0, False, True), (0, True, True)):
            for pages in (1, 3, 64):
                source = root / f"227-{sub}-{battery}-{legacy}-{pages}.nes"
                data = rom(sub, battery, legacy, pages)
                source.write_bytes(data)
                result = run(source)
                assert result.returncode == 0 and "Ran 2 frames successfully" in result.stderr, result.stderr
                assert source.read_bytes() == data
                save = source.with_suffix(".sav")
                if battery:
                    first = save.read_bytes()
                    assert len(first) == 8192 and first[0] == 0xC7
                    assert first[0x123] == 0x5A and first[-1] == 1
                    result = run(source)
                    assert result.returncode == 0, result.stderr
                    second = save.read_bytes()
                    assert second[:-1] == first[:-1] and second[-1] == 2
                    persistent += 1
                else:
                    assert not save.exists()
                valid += 1
        for pads in range(16):
            source = root / f"menu-{pads}.nes"
            data = rom(sub=1, pages=1)
            source.write_bytes(data)
            result = run(source, f"menu-{pads}")
            assert result.returncode == 0, result.stderr
            assert source.read_bytes() == data and not source.with_suffix(".sav").exists()
            valid += 1
        for offset, value in ((8, 0x30), (8, 0xF0), (6, 0x38), (10, 7), (10, 0x70),
                              (11, 0), (11, 6), (11, 8), (11, 0x70)):
            data = rom(pages=1)
            data[offset] = value
            reject(data)
        for sub in (1, 2):
            reject(rom(sub=sub, battery=True, pages=1))
        data = rom(battery=True, pages=1)
        data[10] = 0
        reject(data)
        for pages, chr_pages in ((0, 0), (65, 0), (1, 1)):
            reject(rom(pages=pages, chr_pages=chr_pages))
        for profile in ("menu-", "menu-16", "menu--1", "menu-1x", "other"):
            reject(rom(sub=1, pages=1), profile)
        reject(rom(pages=1), "menu-1")
        reject(rom(sub=2, pages=1), "menu-1")
    print(f"Mapper 227: {valid} loading profiles, {persistent} battery reloads, "
          f"and {invalid} rejected inputs passed")


if __name__ == "__main__":
    main()
