"""Decathlon loading probes; C++ fixtures assert CPU/RMW/PPU completion."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

PRG = ((0, 1, 2, 3), (3, 2, 1, 0), (0, 2, 1, 3), (3, 1, 2, 0))
CHR = ((0, 1, 2, 3, 4, 5, 6, 7), (0, 2, 1, 3, 4, 6, 5, 7),
       (0, 1, 4, 5, 2, 3, 6, 7), (0, 4, 1, 5, 2, 6, 3, 7),
       (0, 4, 2, 6, 1, 5, 3, 7), (0, 2, 4, 6, 1, 3, 5, 7),
       (7, 6, 5, 4, 3, 2, 1, 0), (7, 6, 5, 4, 3, 2, 1, 0))


def rom(legacy=False, vertical=True, final=0x7F):
    header = bytearray(b"NES\x1a" + bytes(12))
    header[4], header[5] = 8, 8
    header[6], header[7] = 0x40 | int(vertical), 0xF0 if legacy else 0xF8
    code = bytearray([0x78])

    def store(address, value):
        code.extend([0xA9, value, 0x8D, address & 255, address >> 8])

    def expect(address, value):
        code.extend([0xAD, address & 255, address >> 8, 0xC9, value, 0xD0, 0xFE])

    def pread(value):
        store(0x2006, 0)
        store(0x2006, 0)
        code.extend([0xAD, 7, 0x20])
        expect(0x2007, value)

    store(0x4017, 0x40)
    for mode, row in enumerate(PRG):
        for bank, expected in enumerate(row):
            store(0x8200, mode * 16 + bank)
            expect(0x9A00, expected)
    for mode, row in enumerate(CHR):
        for bank, expected in enumerate(row):
            store(0x8200, mode * 16 + 8 + bank)
            pread(expected)
    store(0x8200, 13)
    store(0x8100, 0x2F)
    expect(0x9A00, 0)
    pread(5)
    store(0x8200, 0)
    code.extend([0xEE, 0, 0x83])
    expect(0x9A00, 1)
    store(0x8200, final)
    if final & 8:
        pread(CHR[(final >> 4) & 7][final & 7])
    else:
        expect(0x9A00, PRG[(final >> 4) & 3][final & 3])
    store(0x12, 0xC7)
    loop = 0xE000 + len(code)
    code.extend([0x4C, loop & 255, loop >> 8])
    assert len(code) < 0x1800
    prg = bytearray()
    for bank in range(4):
        data = bytearray([bank]) * 32768
        data[0x100:0x200] = bytes(range(256))
        data[0x200] = 255
        data[0x300] = 2 if bank == 0 else 1 if bank == 2 else 255
        data[0x6000:0x6000 + len(code)] = code
        data[-6:] = bytes([0, 0xE0] * 3)
        prg.extend(data)
    return header + prg + b"".join(bytes([bank]) * 8192 for bank in range(8))


def main():
    binary = Path(sys.argv[1]).resolve()
    valid = invalid = 0
    with tempfile.TemporaryDirectory(prefix="mapperbus-244-cli-") as temp:
        root = Path(temp)
        env = dict(os.environ, XDG_CONFIG_HOME=str(root / "config"), XDG_DATA_HOME=str(root / "data"))

        def probe(data, accepted=True, profile=None):
            nonlocal valid, invalid
            source = root / "probe.nes"
            source.write_bytes(data)
            args = [str(binary), str(source), "--frames", "2"]
            if profile is not None:
                args.extend(["--board-profile", profile])
            result = subprocess.run(args, env=env, capture_output=True, text=True, timeout=15, check=False)
            if accepted:
                assert result.returncode == 0 and "Ran 2 frames successfully" in result.stderr, result.stderr
                valid += 1
            else:
                assert result.returncode != 0, result.stderr
                invalid += 1
            assert source.read_bytes() == data and not source.with_suffix(".sav").exists()

        for legacy in (False, True):
            for vertical in (False, True):
                for final in (0, 0x1B, 0x38, 0x68, 0x7F):
                    probe(rom(legacy, vertical, final))
            for offset, value in ((4, 0), (4, 4), (4, 7), (4, 9), (5, 0), (5, 4), (5, 9), (6, 0x42), (6, 0x48)):
                data = rom(legacy=legacy)
                data[offset] = value
                probe(data, accepted=False)
        for offset, value in ((8, 0x10), (8, 0xF0), (10, 7), (10, 0x70), (11, 7), (11, 0x70)):
            data = rom()
            data[offset] = value
            probe(data, accepted=False)
        for profile in ("unknown", "no-conflicts"):
            probe(rom(), accepted=False, profile=profile)
    print(f"Mapper 244: {valid} loading profiles and {invalid} rejected inputs passed")


if __name__ == "__main__":
    main()
