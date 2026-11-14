"""Original G0151-1 firmware: persistence signatures prove banking and BRK completion."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def rom(legacy=False, vertical=True, last_bank=32):
    header = bytearray(b"NES\x1a" + bytes(12))
    header[4], header[5] = 32, 64
    header[6], header[7] = 0x62 | int(vertical), 0xF0 if legacy else 0xF8
    if not legacy:
        header[10] = 0x50
    code = bytearray([0x78, 0xD8, 0xA2, 0xFF, 0x9A])

    def store(address, value):
        code.extend([0xA9, value, 0x8D, address & 255, address >> 8])

    def expect(address, value):
        code.extend([0xAD, address & 255, address >> 8, 0xC9, value, 0xD0, 0xFE])

    def paddr(address):
        store(0x2006, address >> 8)
        store(0x2006, address & 255)

    store(0x4017, 0x40)
    store(0x11, 0)
    for bank in (0, 15, 16, 31, 32, 47, 48, 255):
        for slot in range(4):
            store(0x6018 + slot, bank)
            expect(0x9A00 + slot * 8192, bank & 63)
        expect(0xFFE4, (bank & 63) | 16)
        expect(0xFFE8, bank & 63)
    store(0x6003, last_bank)
    for slot, bank in enumerate((15, 31, 127, 255)):
        store(0x601C + slot, bank)
        paddr(slot * 2048)
        code.extend([0xAD, 7, 0x20])
        expect(0x2007, bank)
    store(0x6024, 0)
    store(0x67FC, 0)
    paddr(0)
    store(0x2007, 0)
    paddr(0)
    code.extend([0xAD, 7, 0x20])
    expect(0x2007, 15)
    code.extend([0, 0xEA])
    expect(0x11, 1)
    expect(0xFA00, last_bank)
    store(0x6800, 0xA7)
    store(0x6FFF, 0x91)
    loop = 0xE000 + len(code)
    code.extend([0x4C, loop & 255, loop >> 8])
    assert len(code) < 0x1000
    prg = bytearray()
    for bank in range(64):
        page = bytearray([bank]) * 8192
        page[:len(code)] = code
        page[0x1000:0x1003] = bytes([0xE6, 0x11, 0x40])
        page[0x1100:0x1103] = bytes([0x4C, 0, 0xF1])
        page[-6:] = bytes([0, 0xE0, 0, 0xE0, 0, 0xF0 if bank & 16 else 0xF1])
        prg.extend(page)
    return header + prg + b"".join(bytes([bank]) * 2048 for bank in range(256))


def main():
    binary = Path(sys.argv[1]).resolve()
    valid = invalid = 0
    with tempfile.TemporaryDirectory(prefix="mapperbus-246-cli-") as temp:
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
                assert len(persisted) == 2048 and persisted[0] == 0xA7 and persisted[-1] == 0x91
                valid += 1
            else:
                assert result.returncode != 0 and not save.exists(), result.stderr
                invalid += 1
            assert source.read_bytes() == data

        for legacy in (False, True):
            for vertical in (False, True):
                for bank in (0, 32, 63):
                    probe(rom(legacy, vertical, bank))
            for offset, value in ((4, 0), (4, 16), (4, 33), (5, 0), (5, 32), (5, 65),
                                  (6, 0x60), (6, 0x6A)):
                data = rom(legacy=legacy)
                data[offset] = value
                probe(data, accepted=False)
        for offset, value in ((8, 0x10), (8, 0xF0), (10, 0), (10, 5), (10, 0x70),
                              (10, 0x55), (11, 7), (11, 0x50)):
            data = rom()
            data[offset] = value
            probe(data, accepted=False)
        for profile in ("unknown", "no-conflicts"):
            probe(rom(), accepted=False, profile=profile)
    print(f"Mapper 246: {valid} execution profiles and {invalid} rejected inputs passed")


if __name__ == "__main__":
    main()
