"""Original FS303 CPU programs verify dynamic CHR windows and physical WRAM saves."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def rom(legacy=False, memory="none", prg_size=524288, chr_size=262144, vertical=True):
    battery = memory in ("battery", "aux-battery")
    aux = memory in ("aux", "aux-battery")
    header = bytearray(b"NES\x1a" + bytes(12))
    header[4], header[5] = prg_size // 16384, chr_size // 8192
    header[6] = 0x30 | int(vertical) | (2 if battery else 0)
    header[7] = 0xC0 if legacy else 0xC8
    if not legacy:
        header[10] = (0x70 if battery else 0) | (6 if aux else 7 if memory == "volatile" else 0)
        header[11] = 7
    prg = bytearray().join(bytes([page]) * 8192 for page in range(prg_size // 8192))
    chr_rom = bytearray().join(bytes([page]) * 1024 for page in range(chr_size // 1024))
    chr_rom[:1024] = bytes([0x5D]) * 1024
    code = bytearray([0x78])

    def store(address, value):
        code.extend([0xA9, value, 0x8D, address & 255, address >> 8])

    def expect(address, value):
        code.extend([0xAD, address & 255, address >> 8, 0xC9, value, 0xD0, 0xFE])

    def bank(reg, value, mode=0):
        store(0x8000, reg | mode)
        store(0x8001, value)

    def address(value):
        store(0x2006, value >> 8)
        store(0x2006, value & 255)

    def pwrite(where, value):
        address(where)
        store(0x2007, value)

    def pread(where, value):
        address(where)
        code.extend([0xAD, 7, 0x20])
        expect(0x2007, value)

    def select(value):
        bank(2, value)
        pwrite(0x1000, 0xFF)  # PPU data is independent of the command bank.

    def rom_byte(page):
        page %= chr_size // 1024
        return page if page else 0x5D

    pwrite(0, 0)
    pread(0, 0x5D)
    bank(6, 2)
    bank(7, 5)
    expect(0x9A00, 2 % (prg_size // 8192))
    expect(0xBA00, 5 % (prg_size // 8192))
    bank(6, 2, 0x40)
    expect(0x9A00, prg_size // 8192 - 2)
    expect(0xDA00, 2 % (prg_size // 8192))
    bank(3, 0x28)
    pwrite(0x1400, 0xD0)
    bank(2, 0x82)
    pread(0x1000, rom_byte(0x82))
    pread(0x1400, 0xD0)  # Reads and register writes did not change the window.
    pwrite(0x1000, 0)
    bank(3, 0)
    pread(0x1400, 0xD0)
    for cmd, first in ((0x82, 0), (0x88, 0x4C)):
        select(cmd)
        for page in range(first, first + 4):
            bank(3, page)
            pwrite(0x1400, 0xD0 + (page & 7))
    for cmd, first in ((0x80, 0x28), (0x82, 0), (0x88, 0x4C), (0x8A, 0x64),
                       (0xC0, 0x46), (0xC2, 0x7C), (0xC8, 0xA)):
        select(cmd)
        bank(0, first | 1, 0x80)
        pread(0x1000, 0xD0 + (first & 7))
        pread(0x1400, 0xD1 + (first & 7))
        bank(3, first, 0x80)
        pread(0x400, 0xD0 + (first & 7))
    for cmd in (0xCA, 0x90, 0xD0):
        select(cmd)
        bank(3, 0)
        pwrite(0x1400, 0)
        pread(0x1400, 0x5D)
    select(0x82)
    bank(3, 0)
    pread(0x1400, 0xD0)
    for gate in (0, 0xC0, 0x80):
        store(0xA001, gate)
        pwrite(0x1400, gate)
        pread(0x1400, gate)
        if aux:
            store(0x5000, gate)
            store(0x5FFF, gate ^ 0x73)
            expect(0x5000, gate)
            expect(0x5FFF, gate ^ 0x73)
    if battery or memory == "volatile":
        store(0x6000, 0x59)
        expect(0x6000, 0x59)
        store(0xA001, 0xC0)
        store(0x6000, 0)
        expect(0x6000, 0x59)
        store(0xA001, 0x80)
    store(0x6000, 0xA7)  # Save marker is reached only after all CPU assertions.
    loop = 0xE000 + len(code)
    code.extend([0x4C, loop & 255, loop >> 8])
    assert len(code) < 0x1800
    for page in range(0, prg_size, 8192):
        prg[page:page + len(code)] = code
        prg[page + 8186:page + 8192] = bytes([0, 0xE0] * 3)
    return header + prg + chr_rom


def main():
    binary = Path(sys.argv[1]).resolve()
    valid_count = save_count = invalid_count = 0
    with tempfile.TemporaryDirectory(prefix="mapperbus-fs303-cli-") as temp:
        root = Path(temp)
        env = dict(os.environ, XDG_CONFIG_HOME=str(root / "config"),
                   XDG_DATA_HOME=str(root / "data"))

        def run(source):
            return subprocess.run([str(binary), str(source), "--frames", "2"],
                                  env=env, capture_output=True, text=True,
                                  timeout=15, check=False)

        for legacy in (False, True):
            memories = ("none", "battery") if legacy else ("none", "volatile", "battery", "aux", "aux-battery")
            for memory in memories:
                for vertical in (False, True):
                    for index, (p, c) in enumerate(((32768, 8192), (524288, 262144),
                                                   (262144, 131072), (98304, 196608))):
                        source = root / f"fs303-{legacy}-{memory}-{vertical}-{index}.nes"
                        original = rom(legacy, memory, p, c, vertical)
                        source.write_bytes(original)
                        result = run(source)
                        assert result.returncode == 0 and "Ran 2 frames successfully" in result.stderr, result.stderr
                        save = source.with_suffix(".sav")
                        if "battery" in memory:
                            assert save.exists() and len(save.read_bytes()) == 8192
                            assert save.read_bytes()[0] == 0xA7, (source.name, "CPU verification did not finish")
                            payload = bytearray(save.read_bytes())
                            payload[-1] = 0x59
                            save.write_bytes(payload)
                            result = run(source)
                            assert result.returncode == 0, result.stderr
                            assert save.read_bytes() == payload
                            save_count += 1
                        else:
                            assert not save.exists()
                        assert source.read_bytes() == original
                        valid_count += 1
        changes = [(8, 0x10), (11, 0), (11, 5), (11, 9), (11, 0x70),
                   (10, 5), (10, 8), (10, 0x60), (10, 0x70), (6, 0x33),
                   (6, 0x39), (4, 1)]
        for index, (offset, value) in enumerate(changes):
            bad = rom()
            bad[offset] = value
            source = root / f"invalid-fs303-{index}.nes"
            source.write_bytes(bad)
            assert run(source).returncode != 0
            assert source.read_bytes() == bad and not source.with_suffix(".sav").exists()
            invalid_count += 1
        for kind in ("chr-absent", "large-prg", "large-chr", "double-wram", "battery-aux-only"):
            bad = rom(memory="battery")
            if kind == "chr-absent":
                bad[5] = 0
                bad = bad[:16 + 524288]
            elif kind == "double-wram":
                bad[10] = 0x77
            elif kind == "battery-aux-only":
                bad[10] = 6
            else:
                bad[4 if kind == "large-prg" else 5] = 33
                bad.extend(bytes(16384 if kind == "large-prg" else 8192))
            source = root / f"invalid-fs303-{kind}.nes"
            source.write_bytes(bad)
            assert run(source).returncode != 0
            assert source.read_bytes() == bad and not source.with_suffix(".sav").exists()
            invalid_count += 1
    print(f"CLI FS303: {valid_count} header/memory profiles, "
          f"{save_count} CPU-verified save/reload cases and {invalid_count} invalid headers passed")


if __name__ == "__main__":
    main()
