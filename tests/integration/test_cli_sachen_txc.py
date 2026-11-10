"""Original loops smoke-test loading every Sachen/TXC header and RAM profile."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


SIZES = {
    36: (131072, 131072), 132: (65536, 32768), 133: (65536, 32768),
    135: (262144, 262144), 136: (65536, 65536), 137: (262144, 32768),
    138: (262144, 131072), 139: (262144, 524288), 141: (262144, 262144),
    143: (32768, 8192), 146: (65536, 65536), 147: (131072, 131072),
    150: (131072, 65536), 172: (32768, 32768), 173: (32768, 65536),
    243: (131072, 131072),
}


def rom(mapper, legacy=False, ram=False, small=False):
    prg_size, chr_size = SIZES[mapper]
    if ram:
        chr_size = 0
    if small:
        prg_size = 65536
    header = bytearray(b"NES\x1a" + bytes(12))
    header[4], header[5] = prg_size // 16384, chr_size // 8192
    header[6] = (mapper & 15) << 4
    header[7] = (mapper & 0xF0) | (0 if legacy else 8)
    if ram and not legacy:
        header[11] = 7
    prg = bytearray(prg_size)
    for bank in range(0, prg_size, 32768):
        prg[bank + 0x6000:bank + 0x6003] = bytes([0x4C, 0, 0xE0])
        prg[bank + 0x7FFA:bank + 0x8000] = bytes([0, 0xE0] * 3)
    return header + prg + bytes(chr_size)


def main():
    binary = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="mapperbus-sachen-cli-") as temp:
        root = Path(temp)
        env = dict(os.environ, XDG_CONFIG_HOME=str(root / "config"))
        def run(path):
            return subprocess.run([str(binary), str(path), "--frames", "2"],
                                  env=env, capture_output=True, text=True,
                                  timeout=15, check=False)
        for mapper in SIZES:
            for legacy in (False, True):
                source = root / f"{mapper}-{legacy}.nes"
                original = rom(mapper, legacy)
                source.write_bytes(original)
                result = run(source)
                assert result.returncode == 0 and "Ran 2 frames successfully" in result.stderr, result.stderr
                assert source.read_bytes() == original and not source.with_suffix(".sav").exists()
            source = root / f"bad-{mapper}.nes"
            invalid = rom(mapper)
            invalid[8] = 0x10  # No submapper 1 is assigned to these boards.
            source.write_bytes(invalid)
            result = run(source)
            assert result.returncode != 0 and "submapper" in result.stderr.lower(), result.stderr
        for label, mapper, ram, small in [("qboy", 141, True, False), ("latch147", 147, False, True)]:
            for legacy in (False, True):
                source = root / f"{label}-{legacy}.nes"
                source.write_bytes(rom(mapper, legacy, ram, small))
                result = run(source)
                assert result.returncode == 0, result.stderr
                assert not source.with_suffix(".sav").exists()
    print("CLI Sachen/TXC: 36 load/boot profiles and 16 rejected submappers passed")


if __name__ == "__main__":
    main()
