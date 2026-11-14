"""Original Contra Fighter loading probes; CPU/IRQ completion is checked in C++."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def rom(legacy=False, vertical=True, final_data=3, prg_pages=8, chr_pages=32):
    header = bytearray(b"NES\x1a" + bytes(12))
    header[4], header[5] = prg_pages, chr_pages
    header[6], header[7] = 0xE0 | int(vertical), 0xE0 if legacy else 0xE8
    code = bytearray([0x78])

    def store(address, value):
        code.extend([0xA9, value, 0x8D, address & 255, address >> 8])

    def expect(address, value):
        code.extend([0xAD, address & 255, address >> 8, 0xC9, value, 0xD0, 0xFE])

    answers = (0, 2, 2, 3)
    store(0x4017, 0x40)
    for address in (0x4020, 0x4FFF, 0x5000, 0x5FFF, 0x6000, 0x7FFF):
        for data in range(4):
            store(address, 0xFC | data)
            expect(0x5000, answers[data])
            expect(0x7000, answers[data])
    for mode in (0, 0x40):
        store(0x8000, mode | 6)
        store(0x8001, 3)
        store(0x8000, mode | 7)
        store(0x8001, 5)
        expect(0x9A00, 3 if mode == 0 else 14)
        expect(0xBA00, 5)
        expect(0xDA00, 14 if mode == 0 else 3)
        expect(0xFA00, 15)
    store(0x8000, 0)
    store(0x8001, 254)
    for address, value in ((0x23, 254), (0x423, 255)):
        store(0x2006, address >> 8)
        store(0x2006, address & 255)
        code.extend([0xAD, 7, 0x20])
        expect(0x2007, value)
    store(0xA000, 0 if vertical else 1)
    store(0x7FFF, final_data)
    expect(0x4020, answers[final_data])
    store(0x12, 0xC7)
    loop = 0xE000 + len(code)
    code.extend([0x4C, loop & 255, loop >> 8])
    assert len(code) < 0x1800
    prg = bytearray()
    for page in range(prg_pages * 2):
        data = bytearray([page]) * 8192
        if page == 15:
            data[:len(code)] = code
            data[-6:] = bytes([0, 0xE0] * 3)
        prg.extend(data)
    chr_rom = bytearray()
    for page in range(chr_pages * 8):
        chr_rom.extend(bytes([page & 255]) * 1024)
    return header + prg + chr_rom


def main():
    binary = Path(sys.argv[1]).resolve()
    valid = invalid = 0
    with tempfile.TemporaryDirectory(prefix="mapperbus-238-cli-") as temp:
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

        for legacy in (False, True):
            for vertical in (False, True):
                for final_data in range(4):
                    probe(rom(legacy, vertical, final_data))
            for p, c in ((0, 32), (7, 32), (9, 32), (16, 16), (8, 0), (8, 16), (8, 33), (8, 64)):
                probe(rom(legacy=legacy, prg_pages=p, chr_pages=c), accepted=False)
            for flags in (0xE2, 0xE8):
                data = rom(legacy=legacy)
                data[6] = flags
                probe(data, accepted=False)
        for offset, value in ((8, 0x10), (8, 0xF0), (10, 7), (10, 0x70), (11, 7), (11, 0x70)):
            data = rom()
            data[offset] = value
            probe(data, accepted=False)
        probe(rom(), accepted=False, profile="unknown")
    print(f"Mapper 238: {valid} loading profiles and {invalid} rejected inputs passed")


if __name__ == "__main__":
    main()
