"""Original T9552 programs cover both dump orders, headers and save files."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def rom(profile="canonical", prg_size=524288, chr_size=262144,
        vertical=True, memory="none"):
    canonical, legacy = profile == "canonical", profile == "legacy"
    battery = memory == "battery"
    header = bytearray(b"NES\x1a" + bytes(12))
    header[4], header[5] = prg_size // 16384, chr_size // 8192
    header[6] = (0x40 if canonical else 0x90) | int(vertical) | (2 if battery else 0)
    header[7], header[8] = (8 if canonical else 0xF0 if legacy else 0xF8), (0x50 if canonical else 0)
    if not legacy:
        header[10] = 0x70 if battery else 7 if memory == "volatile" else 0
    prg = bytearray().join(bytes([page]) * 8192 for page in range(prg_size // 8192))
    chr_rom = bytearray().join(bytes([page]) * 1024 for page in range(chr_size // 1024))
    code = bytearray([0x78])
    def store(address, value):
        code.extend([0xA9, value, 0x8D, address & 255, address >> 8])
    def expect(address, value):
        code.extend([0xAD, address & 255, address >> 8, 0xC9, value, 0xD0, 0xFE])
    store(0x8000, 6)
    store(0x8001, 2)
    expect(0x9A00, 16 % (prg_size // 8192))
    store(0x8000, 2)
    store(0x8001, 4)
    store(0xA001, 0)  # T9552 control bypasses the RAM gate.
    for pattern, (prg_bank, chr_bank) in enumerate(zip(
            [16, 8, 2, 4, 16, 8, 2, 4], [8, 32, 4, 16, 64, 8, 64, 4])):
        store(0x5000 + pattern * 0x101, 0xF8 | pattern)
        expect(0x9A00, prg_bank % (prg_size // 8192))
        store(0x2006, 0x10)
        store(0x2006, 0)
        code.extend([0xAD, 7, 0x20])
        expect(0x2007, chr_bank % (chr_size // 1024))
    store(0xA001, 0x80)
    store(0x6000, 0xA7)  # Reached only after all CPU assertions pass.
    loop = 0xE000 + len(code)
    code.extend([0x4C, loop & 255, loop >> 8])
    assert len(code) < 0x1800
    for page in range(0, prg_size, 8192):
        prg[page:page + len(code)] = code
        prg[page + 8186:page + 8192] = bytes([0, 0xE0] * 3)
    if not canonical:
        # Reorder physical address pins into pattern-zero file order. In full
        # 256/512 KiB fixtures this is a bijection; smaller cases are load smoke.
        physical_prg, physical_chr = bytes(prg), bytes(chr_rom)
        for bank in range(prg_size // 8192):
            physical = ((bank & 0x21) | ((bank & 2) << 3) | ((bank & 4) << 1)
                        | ((bank & 8) >> 2) | ((bank & 16) >> 2)) % (prg_size // 8192)
            prg[bank * 8192:(bank + 1) * 8192] = physical_prg[physical * 8192:(physical + 1) * 8192]
        for bank in range(chr_size // 1024):
            physical = ((bank & 3) | ((bank & 4) << 1) | ((bank & 8) << 4)
                        | ((bank & 16) << 2) | ((bank & 32) >> 3)
                        | ((bank & 64) >> 2) | ((bank & 128) >> 2)) % (chr_size // 1024)
            chr_rom[bank * 1024:(bank + 1) * 1024] = physical_chr[physical * 1024:(physical + 1) * 1024]
    return header + prg + chr_rom


def main():
    binary = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="mapperbus-t9552-cli-") as temp:
        root = Path(temp)
        env = dict(os.environ, XDG_CONFIG_HOME=str(root / "config"),
                   XDG_DATA_HOME=str(root / "data"))
        def run(source):
            return subprocess.run([str(binary), str(source), "--frames", "2"],
                                  env=env, capture_output=True, text=True,
                                  timeout=15, check=False)
        sizes = [(32768, 8192), (524288, 262144), (262144, 262144), (98304, 196608)]
        for profile in ("canonical", "nes2", "legacy"):
            for vertical in (False, True):
                for index, (prg, chr_rom) in enumerate(sizes):
                    memory = "battery" if index in (1, 2) else "none"
                    source = root / f"t9552-{profile}-{vertical}-{index}.nes"
                    original = rom(profile, prg, chr_rom, vertical, memory)
                    source.write_bytes(original)
                    result = run(source)
                    assert result.returncode == 0 and "Ran 2 frames successfully" in result.stderr, result.stderr
                    assert source.read_bytes() == original
                    save = source.with_suffix(".sav")
                    if memory == "battery":
                        assert save.exists() and len(save.read_bytes()) == 8192
                        assert save.read_bytes()[0] == 0xA7, (profile, index, "CPU verification did not finish")
                        # Startup must restore physical WRAM without scrambling.
                        payload = bytearray(save.read_bytes())
                        payload[-1] = 0x59
                        save.write_bytes(payload)
                        assert run(source).returncode == 0
                        assert save.read_bytes()[-1] == 0x59
                    else:
                        assert not save.exists()
        for profile in ("canonical", "nes2"):
            source = root / f"volatile-{profile}.nes"
            source.write_bytes(rom(profile, memory="volatile"))
            assert run(source).returncode == 0 and not source.with_suffix(".sav").exists()
            for index, (byte, value) in enumerate([
                    (11, 7), (11, 0x70), (6, 0x43 if profile == "canonical" else 0x93),
                    (10, 0x70), (10, 6), (10, 8), (10, 0x77),
                    (6, 0x49 if profile == "canonical" else 0x99), (4, 1), (5, 0)]):
                invalid = rom(profile)
                invalid[byte] = value
                rejected = root / f"invalid-{profile}-{index}.nes"
                rejected.write_bytes(invalid)
                assert run(rejected).returncode != 0, (profile, index)
                assert rejected.read_bytes() == invalid and not rejected.with_suffix(".sav").exists()
            for kind in ("large-prg", "large-chr"):
                invalid = rom(profile)
                invalid[4 if kind == "large-prg" else 5] = 33
                invalid.extend(bytes(16384 if kind == "large-prg" else 8192))
                rejected = root / f"invalid-{profile}-{kind}.nes"
                rejected.write_bytes(invalid)
                assert run(rejected).returncode != 0
                assert rejected.read_bytes() == invalid and not rejected.with_suffix(".sav").exists()
        invalid = rom("nes2")
        invalid[8] = 0x10
        rejected = root / "invalid-submapper.nes"
        rejected.write_bytes(invalid)
        assert run(rejected).returncode != 0 and not rejected.with_suffix(".sav").exists()
    print("CLI T9552: 26 header/memory profiles, 12 CPU-verified save/reload cases and 25 invalid headers passed")


if __name__ == "__main__":
    main()
