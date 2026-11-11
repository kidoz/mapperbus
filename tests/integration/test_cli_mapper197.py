"""Mapper-197 load/boot checks; CPU/IRQ assertions live in test_mapper197.cpp."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def rom(profile=0, legacy=False, prg_size=524288, chr_size=524288, vertical=True):
    header = bytearray(b"NES\x1a" + bytes(12))
    header[4], header[5] = prg_size // 16384, chr_size // 8192
    header[6], header[7] = 0x50 | int(vertical), 0xC0 if legacy else 0xC8
    header[8] = 0 if legacy else profile << 4
    prg = bytearray(prg_size)
    code = bytearray([0x78])

    def store(address, value):
        code.extend([0xA9, value, 0x8D, address & 255, address >> 8])

    store(0x8000, 6)
    store(0x8001, 5)
    store(0x8000, 0x47)
    store(0x8001, 2)
    store(0xA001, 0)
    store(0x6000, 8)
    store(0xA001, 0x80)
    store(0x7FFF, 8)
    store(0xA001, 0xC0)
    store(0x6523, 9)
    store(0xA001, 0x80)
    store(0x6000, 9)
    for reg, value in enumerate((0x81, 0x35, 0x93, 0xC7, 0x55, 0x6B)):
        store(0x8000, reg | 0x80)
        store(0x8001, value)
    store(0xA000, 1)
    loop = 0xE000 + len(code)
    code.extend([0x4C, loop & 255, loop >> 8])
    for page in range(0, prg_size, 8192):
        prg[page:page + len(code)] = code
        prg[page + 0x1FFA:page + 0x2000] = bytes([0, 0xE0] * 3)
    return header + prg + bytes(chr_size)


def main():
    binary = Path(sys.argv[1]).resolve()
    valid_count = invalid_count = 0
    with tempfile.TemporaryDirectory(prefix="mapperbus-197-cli-") as temp:
        root = Path(temp)
        env = dict(os.environ, XDG_CONFIG_HOME=str(root / "config"),
                   XDG_DATA_HOME=str(root / "data"))

        def run(source):
            return subprocess.run([str(binary), str(source), "--frames", "2"],
                                  env=env, capture_output=True, text=True,
                                  timeout=15, check=False)

        for legacy, profile in ((True, 0), (False, 0), (False, 1), (False, 2), (False, 3)):
            sizes = ((32768, 8192), (262144 if profile == 3 else 524288, 524288),
                     (131072, 262144), (98304, 196608))
            for vertical in (False, True):
                for index, (p, c) in enumerate(sizes):
                    source = root / f"197-{legacy}-{profile}-{vertical}-{index}.nes"
                    original = rom(profile, legacy, p, c, vertical)
                    source.write_bytes(original)
                    result = run(source)
                    assert result.returncode == 0 and "Ran 2 frames successfully" in result.stderr, result.stderr
                    assert source.read_bytes() == original and not source.with_suffix(".sav").exists()
                    valid_count += 1
        changes = [(8, 0x40), (8, 0xF0), (6, 0x53), (6, 0x59),
                   (10, 7), (10, 0x70), (11, 7), (11, 0x70), (4, 1)]
        for index, (offset, value) in enumerate(changes):
            source = root / f"invalid-197-{index}.nes"
            bad = rom()
            bad[offset] = value
            source.write_bytes(bad)
            assert run(source).returncode != 0
            assert source.read_bytes() == bad and not source.with_suffix(".sav").exists()
            invalid_count += 1
        for kind in ("legacy-battery", "large-prg", "large-chr", "chr-absent", "mux-prg"):
            bad = rom(legacy=kind == "legacy-battery")
            if kind == "legacy-battery":
                bad[6] |= 2
            elif kind == "chr-absent":
                bad[5] = 0
                bad = bad[:16 + 524288]
            elif kind == "mux-prg":
                bad[8] = 0x30
            else:
                bad[4 if kind == "large-prg" else 5] += 1
                bad.extend(bytes(16384 if kind == "large-prg" else 8192))
            source = root / f"invalid-197-{kind}.nes"
            source.write_bytes(bad)
            assert run(source).returncode != 0
            assert source.read_bytes() == bad and not source.with_suffix(".sav").exists()
            invalid_count += 1
    print(f"CLI mapper 197: {valid_count} load/boot profiles and {invalid_count} invalid headers passed")


if __name__ == "__main__":
    main()
