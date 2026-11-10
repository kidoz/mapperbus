"""Original mapper-512 programs self-check banked memory, IRQs and WRAM reloads."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def rom(defaults=False, four_screen=False):
    header = bytearray(b"NES\x1a" + bytes(12))
    header[4], header[5] = 8, 16
    header[6], header[7], header[8] = 3 | (8 if four_screen else 0), 8, 2
    if not defaults:
        header[10], header[11] = 0x70, 7
    code = bytearray([0x78])
    def store(address, value):
        code.extend([0xA9, value, 0x8D, address & 255, address >> 8])
    def expect(address, value):
        code.extend([0xAD, address & 255, address >> 8, 0xC9, value, 0xD0, 0xFE])
    def pwrite(address, value):
        store(0x2006, address >> 8)
        store(0x2006, address & 255)
        store(0x2007, value)
    def pread(address, value):
        store(0x2006, address >> 8)
        store(0x2006, address & 255)
        code.extend([0xAD, 7, 0x20])
        expect(0x2007, value)
    store(0x8000, 6)
    store(0x8001, 3)
    expect(0x8000, 0x93)
    store(0x6000, 0)
    store(0xA001, 0)  # Software never enables MMC3 WRAM; the clone stays enabled.
    store(0x7FFF, 0xA6)
    expect(0x7FFF, 0xA6)
    pwrite(0x2000, 0x19)
    pwrite(0x2400, 0x27)
    store(0x4100, 1)
    for table in range(4):
        pwrite(0x2000 + table * 1024, 0x81 + table)
    store(0x4100, 2)
    pwrite(0, 0x55)
    store(0x4100, 3)
    pread(0x2000, 0x19)
    store(0x4100, 1)
    pread(0x2800, 0x83)
    store(0x4100, 2)
    pread(0, 0x55)
    store(0x4100, 0)
    pread(0, 0x6C)
    code.extend([0xEE, 2, 0x60])
    store(0xC000, 2)
    store(0xC001, 0)
    store(0xE001, 0)
    store(0x2000, 8)  # BG $0000 / sprites $1000 supplies MMC3 A12 edges.
    store(0x2001, 0x18)
    code.extend([0x58, 0xEA, 0xAD, 0, 0x60, 0xC9, 0xA7, 0xD0, 0xF9])
    store(0x4100, 1)
    loop = 0xE000 + len(code)
    code.extend([0x4C, loop & 255, loop >> 8])
    assert len(code) < 0x300
    irq = bytes([0x48, 0xA9, 0, 0x8D, 0, 0xE0, 0xA9, 0xA7, 0x8D, 0, 0x60, 0x68, 0x40])
    prg = bytearray(131072)
    for bank in range(16):
        prg[bank * 8192:(bank + 1) * 8192] = bytes([0x90 + bank]) * 8192
    boot = len(prg) - 8192
    prg[boot:boot + len(code)] = code
    prg[boot + 0x300:boot + 0x300 + len(irq)] = irq
    prg[-6:] = bytes([0, 0xE3, 0, 0xE0, 0, 0xE3])
    return header + prg + bytes([0x6C]) * 131072


def main():
    binary = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="mapperbus-512-cli-") as temp:
        root = Path(temp)
        env = dict(os.environ, XDG_CONFIG_HOME=str(root / "config"))
        def run(source):
            return subprocess.run([str(binary), str(source), "--frames", "2"],
                                  env=env, capture_output=True, text=True,
                                  timeout=15, check=False)
        for defaults in (False, True):
            for four_screen in (False, True):
                source = root / f"512-{defaults}-{four_screen}.nes"
                original = rom(defaults, four_screen)
                source.write_bytes(original)
                for launch in (1, 2):
                    result = run(source)
                    assert result.returncode == 0, result.stderr
                    data = source.with_suffix(".sav").read_bytes()
                    assert len(data) == 8192 and data[0] == 0xA7 and data[2] == launch and data[-1] == 0xA6, (defaults, four_screen, launch, data[0], data[2])
                    assert source.read_bytes() == original
        for index in range(8):
            source = root / f"invalid-{index}.nes"
            invalid = rom()
            if index == 0:
                invalid[6] &= ~2
            elif index == 1:
                invalid[8] = 0x12
            elif index == 2:
                invalid[10] = 0x80
            elif index == 3:
                invalid[10] = 0x77
            elif index == 4:
                invalid[11] = 6
            elif index == 5:
                invalid[11] = 0x70
            elif index == 6:
                invalid[4] = 0
            else:
                invalid[5] = 0
            source.write_bytes(invalid)
            result = run(source)
            assert result.returncode != 0, (index, result.stderr)
            assert not source.with_suffix(".sav").exists() and source.read_bytes() == invalid
    print("CLI mapper 512: eight self-check/reload launches and eight invalid headers passed")


if __name__ == "__main__":
    main()
