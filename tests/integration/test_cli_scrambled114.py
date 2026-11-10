"""Original mapper-114 boot loops cover both assigned wirings and legacy media."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def rom(prg_size=524288, chr_size=524288, submapper=0, legacy=False, vertical=True):
    header = bytearray(b"NES\x1a" + bytes(12))
    header[4], header[5] = prg_size // 16384, chr_size // 8192
    header[6], header[7] = 0x20 | int(vertical), 0x70 if legacy else 0x78
    header[8] = 0 if legacy else submapper << 4
    code = bytearray([0x78])
    def store(address, value):
        code.extend([0xA9, value, 0x8D, address & 255, address >> 8])
    store(0x8000, 0)  # Native WRAM gate, independent of the low controls.
    store(0xA000, 4)  # Written index 4 selects R6 on both submappers.
    store(0x8001 if submapper else 0xC000, 3)
    code.extend([0xAD, 0, 0x80, 0x85, 0x10])
    store(0x6002, 0xCF)  # NROM control alias, not a solder-pad port.
    store(0x6003, 0xFF)  # Outer CHR alias.
    loop = 0xE000 + len(code)
    code.extend([0x4C, loop & 255, loop >> 8])
    prg = bytearray(prg_size)
    for page in range(0, prg_size, 16384):
        prg[page + 8192:page + 8192 + len(code)] = code
        prg[page + 16378:page + 16384] = bytes([0, 0xE0] * 3)
    return header + prg + bytes(chr_size)


def main():
    binary = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="mapperbus-114-cli-") as temp:
        root = Path(temp)
        env = dict(os.environ, XDG_CONFIG_HOME=str(root / "config"))
        def run(source):
            return subprocess.run([str(binary), str(source), "--frames", "2"],
                                  env=env, capture_output=True, text=True,
                                  timeout=15, check=False)
        sizes = [(32768, 8192), (524288, 524288), (262144, 262144), (98304, 196608)]
        for submapper, legacy in [(0, False), (1, False), (0, True)]:
            for vertical in (False, True):
                for index, (prg, chr_rom) in enumerate(sizes):
                    source = root / f"114-{submapper}-{legacy}-{vertical}-{index}.nes"
                    original = rom(prg, chr_rom, submapper, legacy, vertical)
                    source.write_bytes(original)
                    result = run(source)
                    assert result.returncode == 0 and "Ran 2 frames successfully" in result.stderr, result.stderr
                    assert source.read_bytes() == original and not source.with_suffix(".sav").exists()
        for index in range(12):
            source = root / f"invalid-{index}.nes"
            invalid = rom()
            if index < 9:
                byte, value = [(8, 0x20), (6, 0x23), (10, 7), (10, 0x70),
                               (11, 7), (11, 0x70), (6, 0x29), (4, 1), (5, 0)][index]
                invalid[byte] = value
            elif index == 9:
                invalid[4] = 33
                invalid.extend(bytes(16384))
            elif index == 10:
                invalid[5] = 65
                invalid.extend(bytes(8192))
            else:
                invalid[6], invalid[7] = 0x61, 0xB8  # Historical duplicate ID 182.
            source.write_bytes(invalid)
            result = run(source)
            assert result.returncode != 0, (index, result.stderr)
            if index == 11:
                assert "Unsupported mapper: 182" in result.stderr, result.stderr
            assert source.read_bytes() == invalid and not source.with_suffix(".sav").exists()
    print("CLI mapper 114: twenty-four boot profiles and twelve invalid/duplicate headers passed")


if __name__ == "__main__":
    main()
