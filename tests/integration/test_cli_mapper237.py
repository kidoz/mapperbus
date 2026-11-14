"""Original 420-in-1 loading probes; unit/app tests assert CPU completion."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def rom(mode=0, type_mode=False, outer=7, legacy=False, vertical=True, pages=64, chr_pages=0):
    header = bytearray(b"NES\x1a" + bytes(12))
    header[4], header[5] = pages, chr_pages
    header[6], header[7] = 0xD0 | int(vertical), 0xE0 if legacy else 0xE8
    if not legacy:
        header[11] = 7
    code = bytearray([0x78])

    def store(address, value):
        code.extend([0xA9, value, 0x8D, address & 255, address >> 8])

    def expect(address, value):
        code.extend([0xAD, address & 255, address >> 8, 0xC9, value & 255, 0xD0, 0xFE])

    store(0x4017, 0x40)
    store(0x8001, 0)
    expect(0xC000, 2)
    address = 0x8000 + (outer // 4) * 4 + int(type_mode)
    control = mode * 64 + 32 + (outer % 4) * 8

    def banks(inner):
        low = inner & 6 if mode in (1, 3) else inner
        high = 7 if mode < 2 else (inner if mode == 2 else (inner & 6) | 1)
        expect(0x9A80, (outer * 8 + low) * 7 + (2 if type_mode else 0))
        expect(0xFA81, (outer * 8 + high) * 7 + (3 if type_mode else 1))

    for inner in range(8):
        store(address, control + inner)
        banks(inner)
    store(address + 2, control)
    for inner in range(8):
        store(address ^ 7, (control ^ 0xF8) + inner)
        banks(inner)
    for value in (1, 0x23):
        store(0x2006, value)
    store(0x2007, 0x5A)
    for value in (1, 0x23):
        store(0x2006, value)
    code.extend([0xAD, 7, 0x20])
    expect(0x2007, 0x5A)
    store(0x12, 0xC7)
    loop = 0x200 + len(code)
    code.extend([0x4C, loop & 255, loop >> 8])
    assert len(code) < 1536
    boot = bytearray()
    for index, value in enumerate(code):
        address = 0x200 + index
        boot.extend([0xA9, value, 0x8D, address & 255, address >> 8])
    boot.extend([0x4C, 0, 2])
    assert len(boot) < 0x1800
    prg = bytearray()
    for page in range(pages):
        data = bytearray([page]) * 16384
        for base in (0x1A80, 0x3A80):
            data[base:base + 4] = bytes((page * 7 + n) & 255 for n in range(4))
        if page == 7:
            data[:4] = bytes(range(4))
        data[0x2000:0x2000 + len(boot)] = boot
        data[-6:] = bytes([0, 0xE0] * 3)
        prg.extend(data)
    return header + prg + bytes(chr_pages * 8192)


def main():
    binary = Path(sys.argv[1]).resolve()
    valid = invalid = 0
    with tempfile.TemporaryDirectory(prefix="mapperbus-237-cli-") as temp:
        root = Path(temp)
        env = dict(os.environ, XDG_CONFIG_HOME=str(root / "config"),
                   XDG_DATA_HOME=str(root / "data"))

        def probe(data, accepted=True, profile=None):
            nonlocal valid, invalid
            source = root / "probe.nes"
            source.write_bytes(data)
            args = [str(binary), str(source), "--frames", "2"]
            if profile is not None:
                args.extend(["--board-profile", profile])
            result = subprocess.run(args, env=env, capture_output=True, text=True,
                                    timeout=15, check=False)
            if accepted:
                assert result.returncode == 0 and "Ran 2 frames successfully" in result.stderr, result.stderr
                valid += 1
            else:
                assert result.returncode != 0, result.stderr
                invalid += 1
            assert source.read_bytes() == data and not source.with_suffix(".sav").exists()

        for mode in range(4):
            for type_mode in (False, True):
                for outer in (0, 7):
                    for legacy in (False, True):
                        for vertical in (False, True):
                            probe(rom(mode, type_mode, outer, legacy, vertical))
        for offset, value in ((8, 0x10), (8, 0xF0), (6, 0xD2), (6, 0xD8),
                              (10, 7), (10, 0x70), (11, 0), (11, 6), (11, 8), (11, 0x77)):
            data = rom()
            data[offset] = value
            probe(data, accepted=False)
        for legacy in (False, True):
            for pages, chr_pages in ((0, 0), (32, 0), (63, 0), (65, 0), (128, 0), (64, 1)):
                probe(rom(legacy=legacy, pages=pages, chr_pages=chr_pages), accepted=False)
            for flags in (0xD2, 0xD8):
                data = rom(legacy=legacy)
                data[6] = flags
                probe(data, accepted=False)
        for profile in ("unknown", "menu-0", "type-0", "type-3"):
            probe(rom(), accepted=False, profile=profile)
    print(f"Mapper 237: {valid} loading profiles and {invalid} rejected inputs passed")


if __name__ == "__main__":
    main()
