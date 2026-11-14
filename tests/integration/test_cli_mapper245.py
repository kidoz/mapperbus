"""Waixing F003 CLI execution and persistence probes using original firmware."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def rom(legacy=False, vertical=True, size=1048576):
    header = bytearray(b"NES\x1a" + bytes(12))
    header[4] = size // 16384
    header[6], header[7] = 0x52 | int(vertical), 0xF0 if legacy else 0xF8
    if not legacy:
        header[10], header[11] = 0x70, 7
    code = bytearray([0x78])

    def store(address, value):
        code.extend([0xA9, value, 0x8D, address & 255, address >> 8])

    def expect(address, value):
        code.extend([0xAD, address & 255, address >> 8, 0xC9, value, 0xD0, 0xFE])

    store(0x4017, 0x40)
    for mode in range(4):
        for r, value in enumerate((0, 2, 0, 2, 0, 2, 3, 5)):
            store(0x8000, r | mode * 64)
            store(0x8001, value)
        for slot in range(4):
            store(0x2006, slot * 4)
            store(0x2006, 0)
            selector = slot + 2 if mode >= 2 else slot // 2
            outer = (selector & 1) * 64
            banks = (62 if mode & 1 else 3, 5, 3 if mode & 1 else 62, 63)
            for window, bank in enumerate(banks):
                expect(0x9A00 + window * 8192, (outer + bank) % (size // 8192))
    store(0x2006, 0)
    store(0x2006, 0)
    store(0x2007, 0x5A)
    store(0x2006, 0)
    store(0x2006, 0)
    code.extend([0xAD, 7, 0x20])
    expect(0x2007, 0x5A)
    # Persistence proves firmware reached the end of all banking checks.
    store(0x6000, 0xA7)
    store(0x7FFF, 0x91)
    loop = 0xE000 + len(code)
    code.extend([0x4C, loop & 255, loop >> 8])
    assert len(code) < 0x1800
    prg = bytearray()
    for bank in range(size // 8192):
        page = bytearray([bank]) * 8192
        page[:len(code)] = code
        page[-6:] = bytes([0, 0xE0] * 3)
        prg.extend(page)
    return header + prg


def main():
    binary = Path(sys.argv[1]).resolve()
    valid = invalid = 0
    with tempfile.TemporaryDirectory(prefix="mapperbus-245-cli-") as temp:
        root = Path(temp)
        env = dict(os.environ, XDG_CONFIG_HOME=str(root / "config"), XDG_DATA_HOME=str(root / "data"))

        def probe(data, accepted=True, profile=None):
            nonlocal valid, invalid
            source = root / "probe.nes"
            save = source.with_suffix(".sav")
            save.unlink(missing_ok=True)
            source.write_bytes(data)
            args = [str(binary), str(source), "--frames", "2"]
            if profile is not None:
                args.extend(["--board-profile", profile])
            result = subprocess.run(args, env=env, capture_output=True, text=True, timeout=15, check=False)
            if accepted:
                assert result.returncode == 0 and "Ran 2 frames successfully" in result.stderr, result.stderr
                persisted = save.read_bytes()
                assert len(persisted) == 8192 and persisted[0] == 0xA7 and persisted[-1] == 0x91
                valid += 1
            else:
                assert result.returncode != 0 and not save.exists(), result.stderr
                invalid += 1
            assert source.read_bytes() == data

        for legacy in (False, True):
            for vertical in (False, True):
                for size in (32768, 65536, 131072, 262144, 524288, 1048576):
                    probe(rom(legacy, vertical, size))
            for offset, value in ((4, 0), (4, 1), (4, 3), (5, 1), (6, 0x50), (6, 0x5A)):
                data = rom(legacy=legacy)
                data[offset] = value
                probe(data, accepted=False)
        for offset, value in ((8, 0x10), (8, 0xF0), (10, 0), (10, 7), (10, 0x80),
                              (11, 0), (11, 8), (11, 0x70)):
            data = rom()
            data[offset] = value
            probe(data, accepted=False)
        for profile in ("unknown", "no-conflicts"):
            probe(rom(), accepted=False, profile=profile)
    print(f"Mapper 245: {valid} execution profiles and {invalid} rejected inputs passed")


if __name__ == "__main__":
    main()
