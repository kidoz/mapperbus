"""Original mapper-74/192/194 CPU programs check CHR mapping and physical WRAM saves."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def rom(mapper=74, ram=2048, legacy=False, prg_size=524288,
        chr_size=262144, vertical=True, memory="none"):
    battery = memory == "battery"
    ram_only = chr_size == 0
    first = 0 if mapper == 194 else 8
    header = bytearray(b"NES\x1a" + bytes(12))
    header[4], header[5] = prg_size // 16384, chr_size // 8192
    header[6] = ((mapper << 4) & 255) | int(vertical) | (2 if battery else 0)
    header[7] = (mapper & 0xF0) | (0 if legacy else 8)
    if not legacy:
        header[10] = 0x70 if battery else 7 if memory == "volatile" else 0
        header[11] = 5 if ram == 2048 else 6
    prg = bytearray().join(bytes([page]) * 8192 for page in range(prg_size // 8192))
    chr_rom = bytearray().join(bytes([page]) * 1024 for page in range(chr_size // 1024))
    if chr_size:
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
    def rom_byte(page):
        page %= chr_size // 1024
        return page if page else 0x5D
    if mapper == 194:
        pwrite(0, 0xA7)
        pread(0, 0xA7)
    else:
        pwrite(0, 0)
        pwrite(0x1000, 0)
        pread(0, 0x5D)
    bank(6, 2)
    bank(7, 5)
    expect(0x9A00, 2 % (prg_size // 8192))
    expect(0xBA00, 5 % (prg_size // 8192))
    bank(6, 2, 0x40)
    expect(0x9A00, prg_size // 8192 - 2)
    expect(0xDA00, 2 % (prg_size // 8192))
    for page in range(first, first + ram // 1024):
        bank(2, page)
        pwrite(0x1000, 0xA0 + page)
    bank(0, first + 1)
    pread(0, 0xA0 + first)
    pread(0x400, 0xA1 + first)
    bank(1, first + 3)
    pread(0x800, 0xA2 + first if ram == 4096 else 0xA0 if ram_only else rom_byte(first + 2))
    pread(0xC00, 0xA3 + first if ram == 4096 else 0xA1 if ram_only else rom_byte(first + 3))
    bank(2, first, 0x80)
    pread(0, 0xA0 + first)
    for gate in (0, 0xC0):
        store(0xA001, gate)
        pwrite(0, gate)
        pread(0x1000, gate)  # Same physical page through paired and single slots.
    bank(2, first + 0x80, 0x80)
    pwrite(0, 0)
    pread(0, 0 if ram_only else rom_byte(first + 0x80))
    bank(2, first, 0x80)
    pread(0, 0 if ram_only else 0xC0)
    pwrite(0, 0xC0)
    bank(2, first + ram // 1024)
    pwrite(0x1000, 0)
    pread(0x1000, 0 if ram_only else rom_byte(first + ram // 1024))
    store(0xA001, 0x80)
    store(0x6000, 0xA7)  # Reached only after CPU banking assertions pass.
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
    with tempfile.TemporaryDirectory(prefix="mapperbus-waixing-chr-cli-") as temp:
        root = Path(temp)
        env = dict(os.environ, XDG_CONFIG_HOME=str(root / "config"),
                   XDG_DATA_HOME=str(root / "data"))
        def run(source):
            return subprocess.run([str(binary), str(source), "--frames", "2"],
                                  env=env, capture_output=True, text=True,
                                  timeout=15, check=False)
        sizes = [(32768, 8192), (524288, 262144), (262144, 131072), (98304, 196608)]
        for mapper in (74, 192, 194):
            profiles = [(True, 4096 if mapper == 192 else 2048), (False, 2048), (False, 4096)]
            for legacy, ram in profiles:
                for vertical in (False, True):
                    for index, (prg_size, chr_size) in enumerate(sizes):
                        memory = "battery" if index == 1 else "none"
                        source = root / f"mixed-{mapper}-{legacy}-{ram}-{vertical}-{index}.nes"
                        original = rom(mapper, ram, legacy, prg_size, chr_size, vertical, memory)
                        source.write_bytes(original)
                        result = run(source)
                        assert result.returncode == 0 and "Ran 2 frames successfully" in result.stderr, result.stderr
                        save = source.with_suffix(".sav")
                        if memory == "battery":
                            assert save.exists() and len(save.read_bytes()) == 8192
                            assert save.read_bytes()[0] == 0xA7, (mapper, legacy, ram, "CPU verification did not finish")
                            payload = bytearray(save.read_bytes())
                            payload[-1] = 0x59
                            save.write_bytes(payload)
                            assert run(source).returncode == 0
                            assert save.read_bytes()[-1] == 0x59
                            save_count += 1
                        else:
                            assert not save.exists()
                        assert source.read_bytes() == original
                        valid_count += 1
            if mapper == 194:
                for legacy, ram in profiles:
                    for prg_size in (32768, 524288, 98304):
                        for vertical in (False, True):
                            source = root / f"ram-only-{legacy}-{ram}-{prg_size}-{vertical}.nes"
                            original = rom(mapper, ram, legacy, prg_size, 0, vertical, "battery")
                            source.write_bytes(original)
                            result = run(source)
                            assert result.returncode == 0 and "Ran 2 frames successfully" in result.stderr, result.stderr
                            save = source.with_suffix(".sav")
                            assert save.exists() and len(save.read_bytes()) == 8192
                            assert save.read_bytes()[0] == 0xA7, (legacy, ram, prg_size, "RAM-only CPU verification failed")
                            payload = bytearray(save.read_bytes())
                            payload[-1] = 0x59
                            save.write_bytes(payload)
                            assert run(source).returncode == 0 and save.read_bytes()[-1] == 0x59
                            assert source.read_bytes() == original
                            valid_count += 1
                            save_count += 1
            source = root / f"volatile-{mapper}.nes"
            source.write_bytes(rom(mapper, memory="volatile"))
            assert run(source).returncode == 0 and not source.with_suffix(".sav").exists()
            valid_count += 1
            invalid_fields = [(8, 0x10), (11, 0), (11, 4), (11, 7), (11, 0x55),
                              (6, ((mapper << 4) & 255) | 3), (10, 0x70),
                              (10, 6), (10, 8), (10, 0x77),
                              (6, ((mapper << 4) & 255) | 9), (4, 1),
                              (4, 0) if mapper == 194 else (5, 0)]
            for index, (byte, value) in enumerate(invalid_fields):
                invalid = rom(mapper)
                invalid[byte] = value
                rejected = root / f"invalid-{mapper}-{index}.nes"
                rejected.write_bytes(invalid)
                assert run(rejected).returncode != 0, (mapper, index)
                assert rejected.read_bytes() == invalid and not rejected.with_suffix(".sav").exists()
                invalid_count += 1
            for kind in ("large-prg", "large-chr"):
                invalid = rom(mapper)
                invalid[4 if kind == "large-prg" else 5] = 33
                invalid.extend(bytes(16384 if kind == "large-prg" else 8192))
                rejected = root / f"invalid-{mapper}-{kind}.nes"
                rejected.write_bytes(invalid)
                assert run(rejected).returncode != 0
                assert rejected.read_bytes() == invalid and not rejected.with_suffix(".sav").exists()
                invalid_count += 1
    print(f"CLI mixed CHR 74/192/194: {valid_count} header/memory profiles, "
          f"{save_count} CPU-verified save/reload cases and {invalid_count} invalid headers passed")


if __name__ == "__main__":
    main()
