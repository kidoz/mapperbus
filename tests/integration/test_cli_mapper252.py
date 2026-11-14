"""Original VRC4e firmware: battery signatures prove mixed-CHR and IRQ completion."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def rom(legacy=False, battery=True, vertical=True, control=7):
    header = bytearray(b"NES\x1a" + bytes(12))
    header[4], header[5] = 16, 16
    header[6], header[7] = 0xC0 | (2 if battery else 0) | int(vertical), 0xF0 if legacy else 0xF8
    if not legacy:
        header[10], header[11] = 0x70 if battery else 7, 5
    code = bytearray([0x78, 0xD8, 0xA2, 0xFF, 0x9A])

    def store(address, value):
        code.extend([0xA9, value, 0x8D, address & 255, address >> 8])

    def expect(address, value):
        code.extend([0xAD, address & 255, address >> 8, 0xC9, value, 0xD0, 0xFE])

    def paddr(address):
        store(0x2006, address >> 8)
        store(0x2006, address & 255)

    def chr_bank(slot, bank):
        register = 0xB000 + slot // 2 * 4096 + (slot & 1) * 8
        store(register, bank & 15)
        store(register + 4, bank >> 4)

    store(0x4017, 0x40)
    store(0x11, 0)
    for mode in (1, 3):
        store(0x9008, mode)
        for bank in (0, 7, 15, 31, 255):
            store(0x8000, bank)
            store(0xA000, 5)
            expect(0x9A00, bank & 31 if mode == 1 else 30)
            expect(0xBA00, 5)
            expect(0xDA00, 30 if mode == 1 else bank & 31)
    for bank in (6, 7, 5, 0x106):
        for slot in range(8):
            chr_bank(slot, bank)
            paddr(slot * 1024)
            store(0x2007, 0x5A)
            paddr(slot * 1024)
            code.extend([0xAD, 7, 0x20])
            expect(0x2007, 0x5A if bank in (6, 7) else bank & 127)
    store(0xF000, 0 if control & 4 else 15)
    store(0xF004, 13 if control & 4 else 15)
    store(0xF008, control)
    code.extend([0x58, 0xA5, 0x11, 0xC9, 3, 0x90, 0xFA, 0x78])
    store(0xF008, 0)
    store(0x9008, 1)
    store(0x6000, 0xA7)
    store(0x7FFF, 0x91)
    loop = 0xE000 + len(code)
    code.extend([0x4C, loop & 255, loop >> 8])
    assert len(code) < 0x1000
    prg = bytearray().join(bytes([bank]) * 8192 for bank in range(32))
    base = 31 * 8192
    prg[base:base + len(code)] = code
    prg[base + 0x1000:base + 0x100A] = bytes([0x48, 0xE6, 0x11, 0xA9, 0, 0x8D, 0x0C, 0xF0, 0x68, 0x40])
    prg[-6:] = bytes([0, 0xF0, 0, 0xE0, 0, 0xF0])
    return header + prg + b"".join(bytes([bank]) * 1024 for bank in range(128))


def main():
    binary = Path(sys.argv[1]).resolve()
    valid = invalid = 0
    with tempfile.TemporaryDirectory(prefix="mapperbus-252-cli-") as temp:
        root = Path(temp)
        env = dict(os.environ, XDG_CONFIG_HOME=str(root / "config"), XDG_DATA_HOME=str(root / "data"))

        def probe(data, accepted=True, profile=None, error=None):
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
                if data[6] & 2:
                    persisted = save.read_bytes()
                    assert len(persisted) == 8192 and persisted[0] == 0xA7 and persisted[-1] == 0x91
                else:
                    assert not save.exists()
                valid += 1
            else:
                assert result.returncode != 0 and not save.exists(), result.stderr
                if error is not None:
                    assert error in result.stderr, result.stderr
                invalid += 1
            assert source.read_bytes() == data

        for legacy in (False, True):
            for battery in (False, True):
                for vertical in (False, True):
                    for control in (3, 7):
                        probe(rom(legacy, battery, vertical, control))
            for offset, value in ((4, 0), (4, 8), (4, 17), (5, 0), (5, 8), (5, 17), (6, 0xCA)):
                data = rom(legacy=legacy)
                data[offset] = value
                probe(data, accepted=False)
            data = rom(legacy=legacy)
            data[6] = 0xB3
            probe(data, accepted=False, error="Unsupported mapper: 251")
        for offset, value in ((8, 0x10), (8, 0xF0), (10, 0), (10, 7), (10, 0x80),
                              (11, 0), (11, 7), (11, 0x50)):
            data = rom()
            data[offset] = value
            probe(data, accepted=False)
        for profile in ("unknown", "no-conflicts"):
            probe(rom(), accepted=False, profile=profile)
    print(f"Mapper 252: {valid} loading profiles and {invalid} rejected inputs passed")


if __name__ == "__main__":
    main()
