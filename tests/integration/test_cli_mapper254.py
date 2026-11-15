"""Original MMC3 firmware: raw battery signatures prove protection and IRQ completion."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def rom(legacy=False, battery=True, vertical=True, key=0x95):
    header = bytearray(b"NES\x1a" + bytes(12))
    header[4], header[5] = 8, 16
    header[6], header[7] = 0xE0 | (2 if battery else 0) | int(vertical), 0xF0 if legacy else 0xF8
    if not legacy:
        header[10] = 0x70 if battery else 7
    code = bytearray([0x78, 0xD8, 0xA2, 0xFF, 0x9A])

    def store(address, value):
        code.extend([0xA9, value, 0x8D, address & 255, address >> 8])

    def expect(address, value):
        code.extend([0xAD, address & 255, address >> 8, 0xC9, value, 0xD0, 0xFE])

    def reg(index, value, mode=0):
        store(0x8000, index | mode)
        store(0x8001, value)

    def paddr(address):
        store(0x2006, address >> 8)
        store(0x2006, address & 255)

    store(0x4017, 0x40)
    store(0x11, 0)
    store(0xBFFF, 0x80)
    store(0x6000, 0xA5)
    for mask in (key, 0xBF, 0xC0, 0xFF):
        store(0xBFFF, mask)
        expect(0x6000, 0xA5 ^ mask)
    store(0x6000, 0)
    expect(0x6000, 0x5A)
    store(0x9FFE, 0xC6)
    expect(0x6000, 0xA5)
    store(0xBFFF, key)
    expect(0x6000, 0xA5)
    for mode in (0, 0x40):
        for bank in (0, 7, 15, 255):
            reg(6, bank, mode)
            reg(7, 5, mode)
            expect(0x9A00, bank & 15 if mode == 0 else 14)
            expect(0xBA00, 5)
            expect(0xDA00, 14 if mode == 0 else bank & 15)
    for mode in (0, 0x80):
        for index, bank in enumerate((6, 10, 13, 17, 19, 23)):
            reg(index, bank, mode)
        for slot in range(8):
            expected = (6, 7, 10, 11, 13, 17, 19, 23)[slot ^ (4 if mode else 0)]
            paddr(slot * 1024)
            store(0x2007, 0xFF)
            paddr(slot * 1024)
            code.extend([0xAD, 7, 0x20])
            expect(0x2007, expected)
    store(0xC000, 2)
    store(0xC001, 0)
    store(0xE001, 0)
    store(0x2000, 8)
    store(0x2001, 0x18)
    code.extend([0x58, 0xA5, 0x11, 0xC9, 3, 0x90, 0xFA, 0x78])
    store(0xE000, 0)
    store(0x2001, 0)
    store(0xA001, key)
    store(0x6000, 0xA7)
    store(0x7FFF, 0x91)
    loop = 0xE000 + len(code)
    code.extend([0x4C, loop & 255, loop >> 8])
    assert len(code) < 0x1000
    prg = bytearray().join(bytes([bank]) * 8192 for bank in range(16))
    base = 15 * 8192
    prg[base:base + len(code)] = code
    handler = bytes([0x48, 0xE6, 0x11, 0xA9, 0, 0x8D, 0, 0xE0, 0x8D, 1, 0xE0, 0x68, 0x40])
    prg[base + 0x1000:base + 0x1000 + len(handler)] = handler
    prg[-6:] = bytes([0, 0xF0, 0, 0xE0, 0, 0xF0])
    return header + prg + b"".join(bytes([bank]) * 1024 for bank in range(128))


def main():
    binary = Path(sys.argv[1]).resolve()
    valid = invalid = 0
    with tempfile.TemporaryDirectory(prefix="mapperbus-254-cli-") as temp:
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
                if data[6] & 2:
                    persisted = save.read_bytes()
                    assert len(persisted) == 8192 and persisted[0] == 0xA7 and persisted[-1] == 0x91
                else:
                    assert not save.exists()
                valid += 1
            else:
                assert result.returncode != 0 and not save.exists(), result.stderr
                invalid += 1
            assert source.read_bytes() == data

        for legacy in (False, True):
            for battery in (False, True):
                for vertical in (False, True):
                    for key in (0x80, 0xBF):
                        probe(rom(legacy, battery, vertical, key))
        for legacy in (False, True):
            for offset, value in ((4, 0), (4, 4), (4, 16), (5, 0), (5, 8), (5, 32), (6, 0xEA)):
                data = rom(legacy=legacy)
                data[offset] = value
                probe(data, accepted=False)
        for offset, value in ((8, 0x10), (8, 0xF0), (10, 0), (10, 7), (10, 0x80), (11, 7), (11, 0x70)):
            data = rom()
            data[offset] = value
            probe(data, accepted=False)
        for profile in ("unknown", "no-conflicts", "fixed", "auto"):
            probe(rom(), accepted=False, profile=profile)
    print(f"Mapper 254: {valid} loading profiles and {invalid} rejected inputs passed")


if __name__ == "__main__":
    main()
