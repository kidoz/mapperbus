"""Original JY ASIC programs validate real CPU banking/math/IRQs and WRAM saves."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def jy_rom(mapper, legacy=False):
    header = bytearray(b"NES\x1a" + bytes(12))
    header[4], header[5] = 2, 1
    header[6] = ((mapper & 15) << 4) | 2
    header[7] = (mapper & 0xF0) | (0 if legacy else 8)
    if not legacy:
        header[8] = mapper >> 8
        header[10] = 0x70
    code = bytearray([0x78])  # SEI; fixed $E000 boot remains fixed in 8 KiB mode.
    def store(address, value):
        code.extend([0xA9, value, 0x8D, address & 255, address >> 8])
    def expect(address, value):
        # A failed check parks forever before writing the success marker.
        code.extend([0xAD, address & 255, address >> 8, 0xC9, value, 0xD0, 0xFE])
    store(0xD000, 2)
    store(0x8000, 2)
    expect(0x8000, 0x32)
    store(0x5800, 23)
    store(0x5801, 17)
    code.extend([0xEA] * 8)
    expect(0x5800, 135)
    expect(0x5801, 1)
    store(0x5802, 250)
    store(0x5802, 11)
    expect(0x5802, 5)
    if mapper > 255:
        # PCB-specific expected PRG/CHR pages, not computed by mapper code.
        outer, prg_marker, chr_marker = {
            281: (1, 0x30, 0x72), 282: (2, 0x30, 0x70),
            295: (1, 0x33, 0x71), 358: (2, 0x30, 0x74),
            386: (2, 0x30, 0x70), 387: (2, 0x33, 0x70),
            388: (4, 0x30, 0x70), 397: (2, 0x30, 0x72),
        }[mapper]
        store(0xD003 if mapper == 358 else 0xD803, outer)
        expect(0x8000, prg_marker)
        store(0x2006, 0)
        store(0x2006, 0)
        code.extend([0xAD, 7, 0x20])  # Discard buffered PPUDATA byte.
        expect(0x2007, chr_marker)
    store(0x6000, 0)
    for address, value in [(0xC002, 0), (0xC001, 0x40), (0xC004, 0xFE),
                           (0xC005, 0xFF), (0xC003, 0)]:
        store(address, value)
    code.extend([0x58, 0xEA])  # CLI, allow delayed IRQ sampling.
    wait = 0xE000 + len(code)
    code.extend([0xAD, 0, 0x60, 0xC9, 0xA5, 0xD0, 0xF9])
    store(0x7FFF, 0x5A)
    loop = 0xE000 + len(code)
    code.extend([0x4C, loop & 255, loop >> 8])
    assert wait < loop < 0xE100
    irq = bytes([0x48, 0xA9, 0, 0x8D, 2, 0xC0, 0xA9, 0xA5, 0x8D, 0, 0x60, 0x68, 0x40])
    if mapper <= 255:
        prg = bytearray(32768)
        prg[2 * 8192] = 0x32
        prg[0x6000:0x6000 + len(code)] = code
        prg[0x6100:0x6100 + len(irq)] = irq
        prg[-6:] = bytes([0, 0xE1, 0, 0xE0, 0, 0xE1])
        return header + prg + bytes(8192)
    prg = bytearray(524288 if mapper == 387 else 1048576)
    header[4] = len(prg) // 16384
    header[5] = 128
    # Duplicate boot/ISR only at each 128 KiB boundary's fixed last 8 KiB.
    # Outer switches must continue executing, but data markers stay distinct.
    for block in range(0, len(prg), 131072):
        prg[block + 16384] = 0x32 ^ (block // 131072)
        prg[block + 0x1E000:block + 0x1E000 + len(code)] = code
        prg[block + 0x1E100:block + 0x1E100 + len(irq)] = irq
        prg[block + 0x1FFFA:block + 0x20000] = bytes([0, 0xE1, 0, 0xE0, 0, 0xE1])
    chr_rom = bytearray(1048576)
    for block in range(8):
        chr_rom[block * 131072:(block + 1) * 131072] = bytes([0x70 + block]) * 131072
    return header + prg + chr_rom


def main():
    binary = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="mapperbus-jy-cli-") as temp:
        root = Path(temp)
        environment = dict(os.environ, XDG_CONFIG_HOME=str(root / "config"))
        for mapper in (35, 90, 209, 211, 281, 282, 295, 358, 386, 387, 388, 397):
            for legacy in ((False, True) if mapper <= 255 else (False,)):
                source = root / f"{mapper}-{legacy}.nes"
                original = jy_rom(mapper, legacy)
                source.write_bytes(original)
                for _ in range(2):
                    result = subprocess.run([str(binary), str(source), "--frames", "2"],
                                            capture_output=True, text=True, timeout=15,
                                            env=environment, check=False)
                    assert result.returncode == 0, result.stderr
                    save = source.with_suffix(".sav").read_bytes()
                    assert len(save) == 8192 and save[0] == 0xA5 and save[-1] == 0x5A, (mapper, legacy, save[0], save[-1])
                    assert source.read_bytes() == original
    print("CLI JY: 32 launches passed CPU inner/outer banking, PPU data, math, IRQ and WRAM reload checks")


if __name__ == "__main__":
    main()
