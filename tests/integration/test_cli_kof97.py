"""Original native mapper-263 programs verify banking, IRQs and save reloads."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def encode(value):
    # Written D0..D7 reach these standard MMC3 input pins.
    return sum(((value >> actual) & 1) << written
               for written, actual in enumerate((2, 0, 5, 3, 4, 1, 6, 7)))


def rom(nvram=0, volatile=False):
    header = bytearray(b"NES\x1a" + bytes(12))
    header[4], header[5] = 16, 32
    header[6], header[7], header[8] = 0x71 | (2 if nvram else 0), 8, 1
    header[10] = ((nvram.bit_length() - 7) << 4) if nvram else (7 if volatile else 0)
    code = bytearray([0x78])
    def store(address, value):
        code.extend([0xA9, value, 0x8D, address & 255, address >> 8])
    def write(address, logical):
        store(address, encode(logical))
    def expect(address, value):
        code.extend([0xAD, address & 255, address >> 8, 0xC9, value, 0xD0, 0xFE])
    def bank(reg, value):
        write(0x8FFF, reg)
        write(0x9FFF, value)
    def address(target):
        store(0x2006, target >> 8)
        store(0x2006, target & 255)
    def pwrite(target, value):
        address(target)
        store(0x2007, value)
    def pread(target, value):
        address(target)
        code.extend([0xAD, 7, 0x20])
        expect(0x2007, value)

    store(0x4017, 0x40)  # Inhibit APU frame IRQ before enabling mapper IRQs.
    store(0x11, 0)
    write(0xB000, 0x80)
    if nvram:
        store(0x6000, 0)  # Invalidate any previous successful run.
        code.extend([0xEE, 2, 0x60])
        store(0x6003, 0x35)
        expect(0x6003, 0x35)  # WRAM data must not be permuted.
    bank(6, 3)
    bank(7, 5)
    expect(0x8000, 3)
    expect(0xA000, 5)
    expect(0xC000, 30)
    write(0x8001, 0x46)  # A0 is ignored; A12 selects bank data.
    expect(0x8000, 30)
    expect(0xC000, 3)
    for reg, value in enumerate((0x83, 5, 0xE5, 0xA6, 0x37, 0x6B)):
        bank(reg, value)
    pread(0, 0x82)
    pread(0x400, 0x83)
    pread(0x1000, 0xE5)
    pwrite(0x1000, 0x11)
    pread(0x1000, 0xE5)
    write(0x8000, 0x80)
    pread(0, 0xE5)
    pread(0x1000, 0x82)
    pwrite(0x2000, 0x19)
    pwrite(0x2400, 0x27)
    write(0xAFFF, 1)
    pread(0x2400, 0x19)
    write(0xA000, 0)
    pread(0x2400, 0x27)
    write(0xCFFF, 2)
    write(0xD000, 0)
    write(0xFFFF, 0)
    store(0x2000, 8)
    store(0x2001, 0x18)
    code.extend([0x58, 0xEA, 0xAD, 0x11, 0, 0xC9, 0xD3, 0xD0, 0xF9])
    if nvram:
        store(0x6000, 0xC7)  # Only reached after all checks and a mapper IRQ.
    loop = 0xE000 + len(code)
    code.extend([0x4C, loop & 255, loop >> 8])
    assert len(code) < 0x800
    prg = bytearray(262144)
    for page in range(32):
        prg[page * 8192:(page + 1) * 8192] = bytes([page]) * 8192
    prg[0x3E000:0x3E000 + len(code)] = code
    irq = bytes([0x48, 0xA9, 0, 0x8D, 0, 0xE0, 0xA9, 0xD3, 0x85, 0x11, 0x68, 0x40])
    prg[0x3E800:0x3E800 + len(irq)] = irq
    prg[-6:] = bytes([0, 0xE8, 0, 0xE0, 0, 0xE8])
    chr_rom = b"".join(bytes([page]) * 1024 for page in range(256))
    return header + prg + chr_rom


def main():
    binary = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="mapperbus-263-cli-") as temp:
        root = Path(temp)
        env = dict(os.environ, XDG_CONFIG_HOME=str(root / "config"))
        def run(source):
            return subprocess.run([str(binary), str(source), "--frames", "2"],
                                  env=env, capture_output=True, text=True,
                                  timeout=15, check=False)
        for size in (2048, 4096, 8192):
            source = root / f"263-{size}.nes"
            original = rom(size)
            source.write_bytes(original)
            for launch in (1, 2):
                result = run(source)
                assert result.returncode == 0, result.stderr
                data = source.with_suffix(".sav").read_bytes()
                assert len(data) == size and data[0] == 0xC7 and data[2] == launch and data[3] == 0x35, (size, launch)
                assert source.read_bytes() == original
        for volatile in (False, True):
            source = root / f"263-volatile-{volatile}.nes"
            original = rom(volatile=volatile)
            source.write_bytes(original)
            result = run(source)
            assert result.returncode == 0 and "Ran 2 frames successfully" in result.stderr, result.stderr
            assert source.read_bytes() == original and not source.with_suffix(".sav").exists()
        for index, (byte, value) in enumerate([(8, 0x11), (4, 1), (5, 0), (5, 33),
                                               (6, 0x79), (6, 0x73), (10, 0x70),
                                               (10, 8), (11, 7), (11, 0x70)]):
            source = root / f"invalid-{index}.nes"
            invalid = rom()
            invalid[byte] = value
            if byte == 5 and value == 33:
                invalid.extend(bytes(8192))
            source.write_bytes(invalid)
            result = run(source)
            assert result.returncode != 0, (index, result.stderr)
            assert source.read_bytes() == invalid and not source.with_suffix(".sav").exists()
    print("CLI mapper 263: six self-check/reload launches, two boot profiles and ten invalid headers passed")


if __name__ == "__main__":
    main()
