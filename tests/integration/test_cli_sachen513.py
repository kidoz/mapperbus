"""Original mapper-513 programs check PPU-driven PRG banks and battery CHR RAM."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def rom(size=0x180000, defaults=False):
    header = bytearray(b"NES\x1a" + bytes(12))
    header[4], header[6], header[7], header[8] = size // 16384, 0x13, 8, 2
    if not defaults:
        header[11] = 0x90  # 32 KiB nonvolatile CHR, no CPU WRAM.
    code = bytearray([0x78])
    def store(address, value):
        code.extend([0xA9, value, 0x8D, address & 255, address >> 8])
    def expect(address, value):
        code.extend([0xAD, address & 255, address >> 8, 0xC9, value, 0xD0, 0xFE])
    def bank(reg, value):
        store(0x8000, reg)
        store(0x8001, value)
    def address(value):
        store(0x2006, value >> 8)
        store(0x2006, value & 255)
    def pwrite(target, value):
        address(target)
        store(0x2007, value)

    store(0x11, 0)
    bank(2, 31)
    address(0x1003)
    # Read the persistent launch counter through the PPUDATA read buffer.
    code.extend([0xAD, 7, 0x20, 0xAD, 7, 0x20, 0x18, 0x69, 1, 0x85, 0x10])
    address(0x1003)
    code.extend([0xA5, 0x10, 0x8D, 7, 0x20])
    # Invalidate the success marker on every launch before checking anything.
    pwrite(0x1004, 0)
    bank(2, 0)
    pwrite(0x1000, 0xA5)
    bank(2, 31)
    pwrite(0x1000, 0x5A)
    bank(6, 3)
    bank(7, 5)
    for reg, value in enumerate((0, 2, 4, 5, 6, 7)):
        bank(reg, value | 0x80)
    expect(0x8000, 131)
    expect(0xA000, 133)
    expect(0xC000, 62)
    bank(2, 0xC4)
    address(0x1000)
    expect(0x8000, 131 if size == 0x180000 else 195)
    bank(0, 0x40)
    address(0)
    expect(0x8000, 67)
    address(0x1000)
    expect(0x8000, 131 if size == 0x180000 else 195)
    store(0x8000, 0x86)
    expect(0x8000, 67)  # CHR inversion changes PRG pins immediately.
    for reg, value in enumerate((0, 2, 4, 5, 6, 7)):
        bank(reg, value)
    store(0x8000, 0x46)
    expect(0x8000, 62)
    expect(0xC000, 3)
    store(0x8000, 6)
    store(0xC000, 2)
    store(0xC001, 0)
    store(0xE001, 0)
    store(0x2000, 8)  # BG $0000 / sprites $1000 supplies MMC3 A12 edges.
    store(0x2001, 0x18)
    code.extend([0x58, 0xEA, 0xAD, 0x11, 0, 0xC9, 0xD3, 0xD0, 0xF9])
    store(0x2001, 0)
    code.extend([0xEA] * 8)  # Allow the delayed rendering mask to settle.
    bank(2, 31)
    pwrite(0x1004, 0xC7)  # Only reached after bank checks and an IRQ.
    loop = 0xE000 + len(code)
    code.extend([0x4C, loop & 255, loop >> 8])
    assert len(code) < 0x400
    prg = bytearray(size)
    for page in range(size // 8192):
        prg[page * 8192:(page + 1) * 8192] = bytes([page]) * 8192
    # Both fixed windows address the first 512 KiB, even with outer bits set.
    prg[0x7E000:0x7E000 + len(code)] = code
    irq = bytes([0x48, 0xA9, 0, 0x8D, 0, 0xE0, 0xA9, 0xD3, 0x85, 0x11, 0x68, 0x40])
    prg[0x7E400:0x7E400 + len(irq)] = irq
    prg[0x7FFFA:0x80000] = bytes([0, 0xE4, 0, 0xE0, 0, 0xE4])
    return header + prg


def main():
    binary = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="mapperbus-513-cli-") as temp:
        root = Path(temp)
        env = dict(os.environ, XDG_CONFIG_HOME=str(root / "config"))
        def run(source):
            return subprocess.run([str(binary), str(source), "--frames", "2"],
                                  env=env, capture_output=True, text=True,
                                  timeout=15, check=False)
        for size in (0x180000, 0x200000):
            for defaults in (False, True):
                source = root / f"513-{size}-{defaults}.nes"
                original = rom(size, defaults)
                source.write_bytes(original)
                for launch in (1, 2):
                    result = run(source)
                    assert result.returncode == 0, result.stderr
                    data = source.with_suffix(".sav").read_bytes()
                    assert len(data) == 32768
                    assert data[0] == 0xA5 and data[31 * 1024] == 0x5A
                    assert data[31 * 1024 + 3] == launch and data[31 * 1024 + 4] == 0xC7, (size, defaults, launch)
                    assert source.read_bytes() == original
        for index in range(10):
            source = root / f"invalid-{index}.nes"
            invalid = rom()
            if index == 0:
                invalid[6] &= ~2
            elif index == 1:
                invalid[8] = 0x12
            elif index == 2:
                invalid[10] = 7
            elif index == 3:
                invalid[10] = 0x70
            elif index == 4:
                invalid[11] = 0x80
            elif index == 5:
                invalid[11] = 9
            elif index == 6:
                invalid[6] |= 8
            elif index == 7:
                invalid[4] = 16
            elif index == 8:
                invalid[4] = 33
            else:
                invalid[5] = 1
                invalid.extend(bytes(8192))
            source.write_bytes(invalid)
            result = run(source)
            assert result.returncode != 0, (index, result.stderr)
            assert not source.with_suffix(".sav").exists() and source.read_bytes() == invalid
    print("CLI mapper 513: eight self-check/reload launches and ten invalid headers passed")


if __name__ == "__main__":
    main()
