"""Boot both mapper-250 PCB profiles through raw and ZIP source loading."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import zipfile


def rom(legacy=False):
    header = bytearray(b"NES\x1a" + bytes(12))
    header[4], header[5], header[6], header[7] = 8, 16, 0xA1, 0xF0 if legacy else 0xF8
    prg = bytearray(131072)
    for page in range(16):
        prg[page * 8192:(page + 1) * 8192] = bytes([page]) * 8192
    # Native $8046 selects R6 with PRG inversion, $8403 sets R6=3.
    # Read $C000 (3 on L401, 2 on Queen Bee V), then remain in fixed PRG.
    code = bytes([0x78, 0xA9, 0xFF, 0x8D, 0x46, 0x80, 0x8D, 3, 0x84,
                  0xAD, 0, 0xC0, 0x85, 0x10, 0x4C, 0x0E, 0xE0])
    prg[-8192:-8192 + len(code)] = code
    prg[-6:] = bytes([0, 0xE0] * 3)
    return header + prg + bytes(131072)


def main():
    binary = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="mapperbus-250-cli-") as temp:
        root = Path(temp)
        env = dict(os.environ, XDG_CONFIG_HOME=str(root / "config"),
                   XDG_DATA_HOME=str(root / "data"))
        def run(source, *extra):
            return subprocess.run([str(binary), str(source), "--frames", "2", *extra],
                                  env=env, capture_output=True, text=True,
                                  timeout=15, check=False)
        for legacy in (False, True):
            for profile in ("", "nitra-l401", "nitra-queen-bee-v"):
                for archive in (False, True):
                    source = root / f"250-{legacy}-{profile}.{ 'zip' if archive else 'nes'}"
                    original = rom(legacy)
                    if archive:
                        with zipfile.ZipFile(source, "w") as zipped:
                            zipped.writestr("folder/Board.nes", original)
                    else:
                        source.write_bytes(original)
                    before = source.read_bytes()
                    args = ["--board-profile", profile] if profile else []
                    result = run(source, *args)
                    assert result.returncode == 0 and "Ran 2 frames successfully" in result.stderr, result.stderr
                    assert source.read_bytes() == before and not source.with_suffix(".sav").exists()
        for index, (byte, value) in enumerate([(8, 0x10), (6, 0xA3), (10, 7), (10, 0x70),
                                               (11, 7), (11, 0x70), (6, 0xA9), (5, 0), (4, 1)]):
            source = root / f"invalid-{index}.nes"
            invalid = rom()
            invalid[byte] = value
            source.write_bytes(invalid)
            result = run(source)
            assert result.returncode != 0, (index, result.stderr)
            assert source.read_bytes() == invalid and not source.with_suffix(".sav").exists()
        source = root / "valid.nes"
        source.write_bytes(rom())
        for args in [("--board-profile",), ("--board-profile", ""),
                     ("--board-profile", "--region"), ("--board-profile", "unknown")]:
            assert run(source, *args).returncode != 0
        nrom = bytearray(b"NES\x1a" + bytes(12))
        nrom[4], nrom[5] = 2, 1
        wrong = root / "nrom.nes"
        wrong.write_bytes(nrom + bytes(32768 + 8192))
        assert run(wrong, "--board-profile", "nitra-queen-bee-v").returncode != 0
        disk = root / "disk.fds"
        disk.write_bytes(bytes(16))
        result = run(disk, "--board-profile", "nitra-queen-bee-v")
        assert result.returncode != 0 and "Board profiles require a cartridge" in result.stderr
    print("CLI mapper 250: twelve raw/ZIP boot profiles, nine invalid headers and six invalid profile uses passed")


if __name__ == "__main__":
    main()
