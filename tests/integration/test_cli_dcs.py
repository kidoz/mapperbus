"""Exercise paired DCS launches using original synthetic cartridge programs."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import zipfile


def base_rom():
    header = bytearray(b"NES\x1a" + bytes(12))
    header[4:9] = bytes([8, 1, 0x40, 0x48, 0x10])
    prg = bytearray(128 * 1024)
    # Fixed-bank code: select option, retrigger licensing, read signature.
    program = bytes([
        0xA9, 0x00, 0x8D, 0x00, 0xF0, 0x8D, 0x00, 0x60,
        0xAD, 0x00, 0x80, 0xC9, 0x5A, 0xF0, 0x03,
        0x4C, 0xF1, 0xE0, 0x4C, 0xF0, 0xE0,
    ])
    prg[0x1E000:0x1E000 + len(program)] = program
    prg[0x1E0F0:0x1E0F2] = bytes([0x02, 0x02])
    prg[-6:] = bytes([0, 0xE0] * 3)
    return header + prg + bytes(8192)


def main():
    binary = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="mapperbus-dcs-cli-") as directory:
        root = Path(directory)
        environment = dict(os.environ, XDG_CONFIG_HOME=str(root))
        base = root / "base.nes"
        raw = root / "option.bin"
        wrapped = root / "option.nes"
        archive = root / "base.zip"
        base.write_bytes(base_rom())
        raw.write_bytes(bytes([0x5A]) * 16384)
        header = bytearray(b"NES\x1a" + bytes(12))
        header[4] = 1
        wrapped.write_bytes(header + raw.read_bytes())
        with zipfile.ZipFile(archive, "w") as writer:
            writer.writestr("base.nes", base.read_bytes())

        def run(arguments):
            return subprocess.run(
                [str(binary), *map(str, arguments)], capture_output=True, text=True,
                timeout=15, env=environment, check=False,
            )

        for source, args in ((base, []), (archive, ["--entry", "base.nes"])):
            for option in (raw, wrapped):
                result = run([source, *args, "--dcs-option", option, "--frames", "1"])
                assert result.returncode == 0, result.stderr
                assert "CPU JAM (KIL) at E0F0" in result.stderr, result.stderr
                assert "at E0F1" not in result.stderr, result.stderr
        empty = run([base, "--frames", "1"])
        assert "CPU JAM (KIL) at E0F1" in empty.stderr, empty.stderr
        for args in (["--dcs-option"], ["--dcs-option", root / "missing.bin"],
                     ["--dcs-option", "--frames", "1"]):
            result = run([base, *args])
            assert result.returncode != 0, result.stderr
            assert "dcs" in result.stderr.lower(), result.stderr
            assert "Ran " not in result.stderr, result.stderr
        raw.write_bytes(bytes(8192))
        invalid = run([base, "--dcs-option", raw])
        assert invalid.returncode != 0 and "16, 32, 64, or 128" in invalid.stderr
    print("CLI DCS: four paired launches, empty slot, and invalid inputs passed")


if __name__ == "__main__":
    main()
