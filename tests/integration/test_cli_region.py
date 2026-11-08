"""Measure machine timing through the shipped headless CLI using original 6502 code."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile


def timing_rom(expected, nes2_hybrid=False):
    # NMI increments zero page $00. Count 11-cycle loop iterations between
    # successive NMIs; high byte is $0A NTSC, $0B PAL, $0C Hybrid.
    # Distinct JAM addresses expose the result through existing CPU diagnostics.
    program = bytes([
        0x78,                          # SEI
        0xA9, 0x40, 0x8D, 0x17, 0x40, # inhibit APU frame IRQ
        0xA9, 0x00, 0x85, 0x00,       # clear NMI count
        0xA9, 0x80, 0x8D, 0x00, 0x20, # enable NMI
        0xA2, 0x00, 0xA0, 0x00,       # LDX #0; LDY #0
        0xA5, 0x00,                   # LDA $00
        0xC5, 0x00, 0xF0, 0xFC,       # wait for first NMI
        0xA5, 0x00,                   # LDA $00
        0xE8, 0xD0, 0x01, 0xC8,       # INX; BNE +1; INY
        0xC5, 0x00, 0xF0, 0xF8,       # CMP $00; BEQ measurement loop
        0x98, 0xC9, expected,          # TYA; CMP #expected
        0xF0, 0x03,                   # BEQ success
        0x4C, 0xF1, 0x81,             # JMP $81F1 (failure)
        0x4C, 0xF0, 0x81,             # JMP $81F0 (success)
    ])
    prg = bytearray([0xEA] * 16384)
    prg[:len(program)] = program
    prg[0x100:0x103] = bytes([0xE6, 0x00, 0x40])  # INC $00; RTI
    prg[0x1F0:0x1F2] = bytes([0x02, 0x02])
    prg[-6:] = bytes([0x00, 0x81, 0x00, 0x80, 0x00, 0x81])
    header = bytearray(b"NES\x1a" + bytes(12))
    header[4] = 1
    if nes2_hybrid:
        header[7] = 0x08
        header[11] = 7  # 8 KiB CHR RAM
        header[12] = 3  # NES 2.0 UA6538 timing
    return header + prg


def run(binary, args, environment):
    return subprocess.run(
        [str(binary), *map(str, args)], capture_output=True, text=True,
        timeout=15, env=environment, check=False,
    )


def main():
    binary = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="mapperbus-cli-region-") as directory:
        root = Path(directory)
        environment = dict(os.environ, XDG_CONFIG_HOME=str(root))
        cases = [
            ([], 10, False),
            (["--region", "ntsc"], 10, False),
            (["--region", "pal"], 11, False),
            (["--region", "hybrid"], 12, False),
            (["--region", "Hybrid"], 12, False),
            (["--region", "dendy"], 12, False),
            ([], 12, True),
            (["--region", "ntsc"], 10, True),
        ]
        for index, (arguments, expected, nes2) in enumerate(cases):
            rom = root / f"timing-{index}.nes"
            rom.write_bytes(timing_rom(expected, nes2))
            result = run(binary, [rom, "--frames", "4", *arguments], environment)
            assert result.returncode == 0, result.stderr
            assert "CPU JAM (KIL) at 81F0" in result.stderr, (arguments, result.stderr)
            assert "at 81F1" not in result.stderr, result.stderr
            assert "Ran 4 frames successfully" in result.stderr, result.stderr

        for arguments in (["--region"], ["--region", "invalid"],
                          ["--region", "--frames", "4"]):
            result = run(binary, [rom, *arguments], environment)
            assert result.returncode != 0, (arguments, result.stderr)
            assert "region" in result.stderr, result.stderr
            assert "Ran " not in result.stderr, result.stderr
    print("CLI region: 8 timing measurements and 3 invalid-argument cases passed")


if __name__ == "__main__":
    main()
