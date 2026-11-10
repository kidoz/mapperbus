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
    prg = bytearray(32768)
    prg[2 * 8192] = 0x32
    prg[0x6000:0x6000 + len(code)] = code
    prg[0x6100:0x6100 + len(irq)] = irq
    prg[-6:] = bytes([0, 0xE1, 0, 0xE0, 0, 0xE1])
    return header + prg + bytes(8192)


def main():
    binary = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="mapperbus-jy-cli-") as temp:
        root = Path(temp)
        environment = dict(os.environ, XDG_CONFIG_HOME=str(root / "config"))
        for mapper in (35, 90, 209, 211):
            for legacy in (False, True):
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
    print("CLI JY: 16 NES 2.0/iNES launches passed CPU banking, math, IRQ and WRAM reload checks")


if __name__ == "__main__":
    main()
