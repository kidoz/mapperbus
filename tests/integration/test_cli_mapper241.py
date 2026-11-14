"""Original BxROM fixtures: loading geometry, real battery writes and rejected profiles."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def rom(legacy=False, vertical=True, battery=False, chr_rom=False, prg_size=131072, speech=False):
    header = bytearray(b"NES\x1a" + bytes(12))
    header[4], header[5] = (prg_size // 16384) & 255, int(chr_rom)
    header[6], header[7] = 0x10 | int(vertical) | (2 if battery else 0), 0xF0 if legacy else 0xF8
    if not legacy:
        header[9] = (prg_size // 16384) >> 8
        header[10], header[11] = 0x70 if battery else 7, 0 if chr_rom else 7
    code = bytearray([0x78])

    def store(address, value):
        code.extend([0xA9, value, 0x8D, address & 255, address >> 8])

    def expect(address, value):
        code.extend([0xAD, address & 255, address >> 8, 0xC9, value, 0xD0, 0xFE])

    store(0x4017, 0x40)
    # These writes occur only after the banking/WRAM tests complete.
    for data in (0, 1, 3, 128, 255):
        store(0x8000, data)
        expect(0x9A00, data & (prg_size // 32768 - 1))
        store(0x7FFD, data)
        expect(0x7FFD, data)
    for address, value in ((0x6000, 0xA7), (0x7FFD, 0x5A), (0x7FFE, 0x83), (0x7FFF, 0x91)):
        store(address, value)
    store(0x12, 0xC7)
    loop = 0xE000 + len(code)
    code.extend([0x4C, loop & 255, loop >> 8])
    if speech:
        code = bytearray([0x78])
        store(0x4017, 0x40)
        store(0x5000, 0)
        fields = []

        def bits(value, width):
            fields.extend((value >> i) & 1 for i in range(width - 1, -1, -1))

        bits(14, 4)
        bits(0, 1)
        bits(20, 6)
        for value, width in zip((23, 9, 8, 6, 7, 6, 6, 3, 3, 3), (5, 5, 4, 4, 4, 4, 4, 3, 3, 3)):
            bits(value, width)
        for _ in range(3):
            bits(14, 4)
            bits(1, 1)
            bits(20, 6)
        bits(15, 4)
        for offset in range(0, len(fields), 4):
            nibble = sum(value << i for i, value in enumerate(fields[offset:offset + 4]))
            code.extend([0xAD, 0, 0x50, 0x29, 0x40, 0xF0, 0xF9])
            store(0x5000, 0x50 | nibble)
        for address, value in ((0x6000, 0xA7), (0x7FFD, 0x5A), (0x7FFE, 0x83), (0x7FFF, 0x91)):
            store(address, value)
        loop = 0xE000 + len(code)
        code.extend([0x4C, loop & 255, loop >> 8])
    prg = bytearray()
    for page in range(prg_size // 32768):
        data = bytearray([page]) * 32768
        data[0x6000:0x6000 + len(code)] = code
        data[-6:] = bytes([0, 0xE0] * 3)
        prg.extend(data)
    return header + prg + (bytes([0x23]) * 8192 if chr_rom else b"")


def main():
    binary = Path(sys.argv[1]).resolve()
    valid = invalid = 0
    with tempfile.TemporaryDirectory(prefix="mapperbus-241-cli-") as temp:
        root = Path(temp)
        env = dict(os.environ, XDG_CONFIG_HOME=str(root / "config"), XDG_DATA_HOME=str(root / "data"))

        def probe(data, accepted=True, profile=None, region=None):
            nonlocal valid, invalid
            source = root / "probe.nes"
            save = source.with_suffix(".sav")
            save.unlink(missing_ok=True)
            source.write_bytes(data)
            args = [str(binary), str(source), "--frames", "2"]
            if profile is not None:
                args.extend(["--board-profile", profile])
            if region is not None:
                args.extend(["--region", region])
            result = subprocess.run(args, env=env, capture_output=True, text=True, timeout=15, check=False)
            if accepted:
                assert result.returncode == 0 and "Ran 2 frames successfully" in result.stderr, result.stderr
                valid += 1
                if data[6] & 2:
                    persisted = save.read_bytes()
                    assert len(persisted) == 8192
                    assert (persisted[0], *persisted[-3:]) == (0xA7, 0x5A, 0x83, 0x91)
                else:
                    assert not save.exists()
            else:
                assert result.returncode != 0 and not save.exists(), result.stderr
                invalid += 1
            assert source.read_bytes() == data

        for legacy in (False, True):
            for vertical in (False, True):
                for battery in (False, True):
                    for chr_rom in (False, True):
                        probe(rom(legacy, vertical, battery, chr_rom))
        for size in (32768, 65536, 262144, 524288, 1048576, 2097152, 4194304, 8388608):
            probe(rom(prg_size=size, battery=True))
        for offset, value in ((4, 0), (4, 1), (4, 3), (5, 2), (6, 0x18),
                              (8, 0x10), (8, 0xF0), (10, 0), (10, 6),
                              (10, 0x70), (10, 0x77), (11, 0), (11, 8), (11, 0x70)):
            data = rom()
            data[offset] = value
            probe(data, accepted=False)
        for legacy in (False, True):
            for vertical in (False, True):
                for chr_rom in (False, True):
                    probe(rom(legacy, vertical, True, chr_rom, speech=True), profile="lpc")
        for region in ("pal", "hybrid"):
            probe(rom(battery=True, speech=True), profile="lpc", region=region)
        probe(rom(battery=True), profile="standard")
        probe(rom(), accepted=False, profile="unknown")
    print(f"Mapper 241: {valid} loading profiles and {invalid} rejected inputs passed")


if __name__ == "__main__":
    main()
