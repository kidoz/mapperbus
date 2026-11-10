"""Original H2288 load/boot smoke checks; CPU assertions live in test_h2288.cpp."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def rom(prg_size=524288, chr_size=262144, legacy=False, vertical=True):
    header = bytearray(b"NES\x1a" + bytes(12))
    header[4], header[5] = prg_size // 16384, chr_size // 8192
    header[6], header[7] = 0xB0 | int(vertical), 0x70 if legacy else 0x78
    prg = bytearray(prg_size)
    code = bytearray([0x78])
    def store(address, value):
        code.extend([0xA9, value, 0x8D, address & 255, address >> 8])
    store(0xA001, 0)  # Override works independently of the MMC3 RAM gate.
    store(0x8000, 4)  # Native index 4 selects PRG register 6.
    store(0x8001, 3)
    store(0x5800, 0x75)  # Mirrored 16 KiB bank 15.
    store(0x5FFF, 0xF5)  # Alias and ignored bit 7.
    store(0x6000, 0)     # This is not an H2288 control register.
    loop = 0xE000 + len(code)
    code.extend([0x4C, loop & 255, loop >> 8])
    for page in range(0, prg_size, 16384):
        prg[page + 8192:page + 8192 + len(code)] = code
        prg[page + 16378:page + 16384] = bytes([0, 0xE0] * 3)
    return header + prg + bytes(chr_size)


def main():
    binary = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="mapperbus-123-cli-") as temp:
        root = Path(temp)
        env = dict(os.environ, XDG_CONFIG_HOME=str(root / "config"))
        def run(source):
            return subprocess.run([str(binary), str(source), "--frames", "2"],
                                  env=env, capture_output=True, text=True,
                                  timeout=15, check=False)
        sizes = [(32768, 8192), (524288, 262144), (262144, 131072), (98304, 196608)]
        for legacy in (False, True):
            for vertical in (False, True):
                for index, (prg, chr_rom) in enumerate(sizes):
                    source = root / f"123-{legacy}-{vertical}-{index}.nes"
                    original = rom(prg, chr_rom, legacy, vertical)
                    source.write_bytes(original)
                    result = run(source)
                    assert result.returncode == 0 and "Ran 2 frames successfully" in result.stderr, result.stderr
                    assert source.read_bytes() == original and not source.with_suffix(".sav").exists()
        for index in range(11):
            source = root / f"invalid-{index}.nes"
            invalid = rom()
            if index < 9:
                byte, value = [(8, 0x10), (6, 0xB3), (10, 7), (10, 0x70),
                               (11, 7), (11, 0x70), (6, 0xB9), (4, 1), (5, 0)][index]
                invalid[byte] = value
            elif index == 9:
                invalid[4] = 33
                invalid.extend(bytes(16384))
            elif index == 10:
                invalid[5] = 33
                invalid.extend(bytes(8192))
            source.write_bytes(invalid)
            result = run(source)
            assert result.returncode != 0, (index, result.stderr)
            assert source.read_bytes() == invalid and not source.with_suffix(".sav").exists()
    print("CLI mapper 123: sixteen load/boot profiles and eleven invalid headers passed")


if __name__ == "__main__":
    main()
