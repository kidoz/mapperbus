"""Golden Game loading probes; CPU completion is asserted by unit/app tests."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


MASKS = (1, 3, 5, 7, 9, 11, 13, 15)
PROFILES = tuple("slots-" + "-".join(str(i) for i in range(4) if mask & (1 << i))
                 for mask in MASKS)


def rom(mask=5, legacy=False, vertical=True, floating=False, pages=None, chr_pages=0):
    if pages is None:
        pages = mask.bit_count() * 64
    header = bytearray(b"NES\x1a" + bytes(12))
    header[4], header[5] = pages & 255, chr_pages
    header[6], header[7] = 0xB0 | int(vertical), 0xE0 if legacy else 0xE8
    if not legacy:
        header[9], header[11] = pages >> 8, 7
    code = bytearray([0x78])

    def store(address, value):
        code.extend([0xA9, value, 0x8D, address & 255, address >> 8])

    def expect(address, value):
        code.extend([0xAD, address & 255, address >> 8, 0xC9, value, 0xD0, 0xFE])

    store(0x4017, 0x40)
    if floating:
        hole = next(i for i in range(1, 4) if not mask & (1 << i))
        store(0x8000 + hole * 256, 0xFF)
        expect(0x9A00, 0x9A)
        expect(0xFA00, 0xFA)
        store(0x8000, 0)
        expect(0x9A00, 0)
        expect(0xFA00, 1)
    else:
        packed = 0
        for socket in range(4):
            if not mask & (1 << socket):
                continue
            for bank in (0, 7, 31):
                for small in (False, True):
                    for upper in (False, True):
                        store(0x8000 + socket * 256 + bank + 2048 * small + 4096 * upper, 0xFF)
                        expect(0x9A00, packed * 64 + bank * 2 + int(small and upper))
                        expect(0xFA00, packed * 64 + bank * 2 + (int(upper) if small else 1))
            packed += 1
    # CHR RAM write/read through the PPU's buffered data port.
    for value in (1, 0x23):
        store(0x2006, value)
    store(0x2007, 0xA7)
    for value in (1, 0x23):
        store(0x2006, value)
    code.extend([0xAD, 7, 0x20])
    expect(0x2007, 0xA7)
    store(0x12, 0xD7 if floating else 0xC7)
    loop = (0x400 if floating else 0xE000) + len(code)
    code.extend([0x4C, loop & 255, loop >> 8])
    if floating:
        assert len(code) < 1024
        boot = bytearray()
        for index, value in enumerate(code):
            address = 0x400 + index
            boot.extend([0xA9, value, 0x8D, address & 255, address >> 8])
        boot.extend([0x4C, 0, 4])
    else:
        boot = code
    assert len(boot) < 0x1800
    prg = bytearray()
    for page in range(pages):
        data = bytearray([page & 255]) * 16384
        data[0x2000:0x2000 + len(boot)] = boot
        data[-6:] = bytes([0, 0xE0] * 3)
        prg.extend(data)
    return header + prg + bytes(chr_pages * 8192)


def main():
    binary = Path(sys.argv[1]).resolve()
    valid = invalid = 0
    with tempfile.TemporaryDirectory(prefix="mapperbus-235-cli-") as temp:
        root = Path(temp)
        env = dict(os.environ, XDG_CONFIG_HOME=str(root / "config"),
                   XDG_DATA_HOME=str(root / "data"))

        def probe(data, profile=None, accepted=True):
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

        for mask, profile in zip(MASKS, PROFILES):
            for legacy in (False, True):
                if legacy and mask == 15:
                    continue
                for vertical in (False, True):
                    probe(rom(mask, legacy, vertical), profile)
                if mask != 15:
                    probe(rom(mask, legacy, floating=True), profile)
        for mask in (1, 5, 7, 15):
            for legacy in (False, True):
                if not (legacy and mask == 15):
                    probe(rom(mask, legacy))
        for offset, value in ((8, 0x10), (8, 0xF0), (6, 0xB2), (6, 0xB8),
                              (10, 7), (10, 0x70), (11, 0), (11, 6), (11, 8), (11, 0x77)):
            data = rom()
            data[offset] = value
            probe(data, accepted=False)
        for legacy in (False, True):
            for pages in (0, 32, 96, 129, 255):
                probe(rom(legacy=legacy, pages=pages), accepted=False)
            probe(rom(legacy=legacy, chr_pages=1), accepted=False)
        probe(rom(pages=320), accepted=False)
        for flags in (0xB2, 0xB8):
            data = rom(legacy=True)
            data[6] = flags
            probe(data, accepted=False)
        for profile in ("unknown", "slots-2", "slots-0-0", "slots-0-4", "slots-1-0",
                        "slots-0", "slots-0-1-2", "slots-0-1-2-3"):
            probe(rom(), profile, accepted=False)
    print(f"Mapper 235: {valid} loading profiles and {invalid} rejected inputs passed")


if __name__ == "__main__":
    main()
