"""Original mapper-202 boot smoke tests; CPU correctness is checked by unit fixtures."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def rom(legacy=False, vertical=True, prg_pages=8, chr_pages=8):
    header = bytearray(b"NES\x1a" + bytes(12))
    header[4], header[5] = prg_pages, chr_pages
    header[6], header[7] = 0xA0 | int(vertical), 0xC0 if legacy else 0xC8
    code = bytearray([0x78])
    for latch in range(16):
        code.extend([0xA9, latch ^ 0xA7, 0x8D, 0xF0 | latch, 0xFF])
    loop = 0xE000 + len(code)
    code.extend([0x4C, loop & 255, loop >> 8])
    prg = bytearray()
    for page in range(prg_pages):
        data = bytearray([page]) * 16384
        data[0x2000:0x2000 + len(code)] = code
        data[-6:] = bytes([0, 0xE0] * 3)
        prg.extend(data)
    chr_rom = bytearray().join(bytes([page]) * 8192 for page in range(chr_pages))
    return header + prg + chr_rom


def main():
    binary = Path(sys.argv[1]).resolve()
    valid_count = invalid_count = 0
    with tempfile.TemporaryDirectory(prefix="mapperbus-202-cli-") as temp:
        root = Path(temp)
        env = dict(os.environ, XDG_CONFIG_HOME=str(root / "config"),
                   XDG_DATA_HOME=str(root / "data"))

        def run(source):
            return subprocess.run([str(binary), str(source), "--frames", "2"],
                                  env=env, capture_output=True, text=True,
                                  timeout=15, check=False)

        for legacy in (False, True):
            for vertical in (False, True):
                for p, c in ((1, 1), (3, 3), (8, 8)):
                    source = root / f"202-{legacy}-{vertical}-{p}-{c}.nes"
                    original = rom(legacy, vertical, p, c)
                    source.write_bytes(original)
                    result = run(source)
                    assert result.returncode == 0 and "Ran 2 frames successfully" in result.stderr, result.stderr
                    assert source.read_bytes() == original
                    assert not source.with_suffix(".sav").exists()
                    valid_count += 1
        for offset, value in ((8, 0x10), (8, 0xF0), (6, 0xA3), (6, 0xA9),
                              (10, 7), (10, 0x70), (11, 7), (11, 0x70)):
            bad = rom()
            bad[offset] = value
            source = root / f"invalid-202-{invalid_count}.nes"
            source.write_bytes(bad)
            assert run(source).returncode != 0
            assert source.read_bytes() == bad and not source.with_suffix(".sav").exists()
            invalid_count += 1
        for legacy, p, c in ((False, 9, 8), (False, 8, 9), (False, 8, 0),
                              (True, 9, 8), (True, 8, 9), (True, 8, 0)):
            bad = rom(legacy, prg_pages=p, chr_pages=c)
            source = root / f"invalid-202-{invalid_count}.nes"
            source.write_bytes(bad)
            assert run(source).returncode != 0
            assert source.read_bytes() == bad and not source.with_suffix(".sav").exists()
            invalid_count += 1
    print(f"Mapper 202: {valid_count} boot profiles and {invalid_count} rejected headers passed")


if __name__ == "__main__":
    main()
