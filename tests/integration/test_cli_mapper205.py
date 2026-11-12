"""Mapper-205 loading/profile smoke tests; unit CPU fixtures verify execution."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def rom(legacy=False, vertical=True, prg_pages=32, chr_pages=64):
    header = bytearray(b"NES\x1a" + bytes(12))
    header[4], header[5] = prg_pages, chr_pages
    header[6], header[7] = 0xD0 | int(vertical), 0xC0 if legacy else 0xC8
    code = bytearray([0x78])
    for block in range(4):
        code.extend([0xA9, 0xFC | block, 0x8D, 0, 0x60])
        code.extend([0xA9, 6, 0x8D, 0, 0x80, 0xA9, 0x35, 0x8D, 1, 0x80])
    loop = 0xE000 + len(code)
    code.extend([0x4C, loop & 255, loop >> 8])
    prg = bytearray()
    for page in range(prg_pages * 2):
        data = bytearray([page]) * 8192
        data[:len(code)] = code
        data[-6:] = bytes([0, 0xE0] * 3)
        prg.extend(data)
    chr_rom = bytearray(chr_pages * 8192)
    return header + prg + chr_rom


def main():
    binary = Path(sys.argv[1]).resolve()
    valid_count = invalid_count = 0
    with tempfile.TemporaryDirectory(prefix="mapperbus-205-cli-") as temp:
        root = Path(temp)
        env = dict(os.environ, XDG_CONFIG_HOME=str(root / "config"),
                   XDG_DATA_HOME=str(root / "data"))

        def run(source, profile=None):
            command = [str(binary), str(source), "--frames", "2"]
            if profile is not None:
                command.extend(["--board-profile", profile])
            return subprocess.run(command, env=env, capture_output=True, text=True,
                                  timeout=15, check=False)

        for legacy in (False, True):
            for vertical in (False, True):
                for profile in (None, "jc-016-2", "jc-016-2-force-block3"):
                    source = root / f"205-{legacy}-{vertical}-{profile}.nes"
                    original = rom(legacy, vertical)
                    source.write_bytes(original)
                    result = run(source, profile)
                    assert result.returncode == 0 and "Ran 2 frames successfully" in result.stderr, result.stderr
                    assert source.read_bytes() == original and not source.with_suffix(".sav").exists()
                    valid_count += 1
        for p, c in ((2, 1), (6, 3)):
            source = root / f"small-205-{p}-{c}.nes"
            original = rom(prg_pages=p, chr_pages=c)
            source.write_bytes(original)
            assert run(source).returncode == 0
            assert source.read_bytes() == original and not source.with_suffix(".sav").exists()
            valid_count += 1
        for offset, value in ((8, 0x10), (8, 0xF0), (6, 0xD3), (6, 0xD9),
                              (10, 7), (10, 0x70), (11, 7), (11, 0x70)):
            bad = rom()
            bad[offset] = value
            source = root / f"invalid-205-{invalid_count}.nes"
            source.write_bytes(bad)
            assert run(source).returncode != 0
            assert source.read_bytes() == bad and not source.with_suffix(".sav").exists()
            invalid_count += 1
        for p, c in ((1, 64), (33, 64), (32, 65), (32, 0)):
            bad = rom(prg_pages=p, chr_pages=c)
            source = root / f"invalid-205-{invalid_count}.nes"
            source.write_bytes(bad)
            assert run(source).returncode != 0
            assert source.read_bytes() == bad and not source.with_suffix(".sav").exists()
            invalid_count += 1
        source = root / "profiles.nes"
        original = rom()
        source.write_bytes(original)
        for profile in ("jc-016-2-force-block2", "nitra-l401"):
            assert run(source, profile).returncode != 0
            invalid_count += 1
        assert source.read_bytes() == original and not source.with_suffix(".sav").exists()
    print(f"Mapper 205: {valid_count} boot profiles and {invalid_count} rejected headers/profiles passed")


if __name__ == "__main__":
    main()
