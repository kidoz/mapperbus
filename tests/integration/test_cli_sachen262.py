"""Original mapper-262 boot loops exercise header profiles and volatile storage."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def rom(prg_size=262144, chr_size=524288, defaults=False, mirroring=8):
    header = bytearray(b"NES\x1a" + bytes(12))
    header[4], header[5] = prg_size // 16384, chr_size // 8192
    header[6], header[7], header[8] = 0x60 | mirroring, 8, 1
    if not defaults:
        header[11] = 7  # Fixed four-screen VRAM is excluded from CHR-RAM size.
    prg = bytearray(prg_size)
    if prg_size >= 8192:
        # Select pattern RAM, then run an original JMP loop in fixed PRG.
        code = bytes([0x78, 0xA9, 0x40, 0x8D, 0, 0x41, 0x4C, 6, 0xE0])
        prg[-8192:-8192 + len(code)] = code
        prg[-6:] = bytes([0, 0xE0] * 3)
    return header + prg + bytes(chr_size)


def main():
    binary = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="mapperbus-262-cli-") as temp:
        root = Path(temp)
        env = dict(os.environ, XDG_CONFIG_HOME=str(root / "config"))
        def run(source):
            return subprocess.run([str(binary), str(source), "--frames", "2"],
                                  env=env, capture_output=True, text=True,
                                  timeout=15, check=False)
        profiles = [rom(defaults=defaults, mirroring=mirroring)
                    for defaults in (False, True) for mirroring in (0, 1, 8)]
        profiles += [rom(32768, 8192), rom(524288, 524288), rom(98304, 196608)]
        for index, original in enumerate(profiles):
            source = root / f"262-{index}.nes"
            source.write_bytes(original)
            result = run(source)
            assert result.returncode == 0 and "Ran 2 frames successfully" in result.stderr, result.stderr
            assert source.read_bytes() == original and not source.with_suffix(".sav").exists()
        invalid_profiles = [rom(16384), rom(540672), rom(chr_size=0), rom(chr_size=532480)]
        for byte, value in [(8, 0x11), (6, 0x6A), (10, 7), (10, 0x70),
                            (11, 6), (11, 8), (11, 0x70)]:
            invalid = rom()
            invalid[byte] = value
            invalid_profiles.append(invalid)
        for index, invalid in enumerate(invalid_profiles):
            source = root / f"invalid-{index}.nes"
            source.write_bytes(invalid)
            result = run(source)
            assert result.returncode != 0, (index, result.stderr)
            assert source.read_bytes() == invalid and not source.with_suffix(".sav").exists()
    print("CLI mapper 262: nine boot profiles and eleven invalid headers passed")


if __name__ == "__main__":
    main()
