"""Original polling ROMs exercise CLI Datach input and isolated persistent devices."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import zipfile


def rom(external=False, legacy=False, mapper=157):
    header = bytearray(b"NES\x1a" + bytes(12))
    header[4] = 16
    header[6] = ((mapper & 15) << 4) | (2 if external else 0)
    header[7] = (mapper & 0xF0) | (0 if legacy else 8)
    if not legacy:
        header[10] = 0x10 if external else 0
        header[11] = 7
    prg = bytearray([0xEA]) * 0x40000
    # Poll white D3, then JAM at C0F0 only once a dark bar arrives.
    program = bytes([0xA9, 0x80, 0x8D, 0x0D, 0x80,
                     0xAD, 0, 0x60, 0x29, 8, 0xC9, 8, 0xF0, 0xF7,
                     0x4C, 0xF0, 0xC0])
    prg[0x3C000:0x3C000 + len(program)] = program
    prg[0x3C0F0] = 2
    prg[-6:] = bytes([0, 0xC0] * 3)
    return header + prg


def main():
    binary = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="mapperbus-datach-cli-") as temp:
        root = Path(temp)
        environment = dict(os.environ, XDG_CONFIG_HOME=str(root / "config"),
                           XDG_DATA_HOME=str(root / "data"))

        def run(arguments):
            return subprocess.run([str(binary), *map(str, arguments)], capture_output=True,
                                  text=True, timeout=15, env=environment, check=False)

        unit = root / "reader.sav"
        unit.write_bytes(bytes([0xA5]) * 256)
        launches = 0
        for external in (False, True):
            for legacy in (False, True):
                game = root / f"game-{external}-{legacy}.nes"
                game.write_bytes(rom(external, legacy))
                if external:
                    game.with_suffix(".sav").write_bytes(bytes([0x5A]) * 128)
                archive = game.with_suffix(".zip")
                with zipfile.ZipFile(archive, "w") as writer:
                    writer.writestr("game.nes", game.read_bytes())
                sources = [(game, [])]
                # Archived battery saves use the platform data root. Exercise
                # those in C++ with an explicit persistence root instead.
                if not external:
                    sources.append((archive, ["--entry", "game.nes"]))
                for source, entry in sources:
                    plain = run([source, *entry, "--datach-save", unit, "--frames", 1])
                    assert plain.returncode == 0, plain.stderr
                    assert "CPU JAM" not in plain.stderr, plain.stderr
                    for code, cycles in (("4006381333931", 1000), ("96385074", 2),
                                         ("036000291452", 10)):
                        result = run([source, *entry, "--datach-save", unit,
                                      "--barcode", code, "--barcode-cycles", cycles,
                                      "--barcode-frame", 1, "--frames", 3])
                        assert result.returncode == 0, result.stderr
                        assert "CPU JAM (KIL) at C0F0" in result.stderr, result.stderr
                        launches += 1
                assert unit.read_bytes() == bytes([0xA5]) * 256
                if external:
                    assert game.with_suffix(".sav").read_bytes() == bytes([0x5A]) * 128
                else:
                    assert not game.with_suffix(".sav").exists()
        source = root / "game-False-False.nes"
        for args in (["--barcode"], ["--barcode", "abc"], ["--barcode", "123"],
                     ["--barcode-cycles", "1"], ["--barcode", "96385074", "--barcode-cycles", "0"],
                     ["--barcode", "96385074", "--barcode-frame", "3", "--frames", "3"],
                     ["--datach-save"], ["--datach-save", source.with_suffix(".sav")]):
            result = run([source, "--datach-save", unit, *args, "--frames", "3"])
            assert result.returncode != 0, (args, result.stderr)
        for size in (255, 257):
            unit.write_bytes(bytes(size))
            result = run([source, "--datach-save", unit, "--frames", 1])
            assert result.returncode != 0 and "Datach" in result.stderr, result.stderr
            assert len(unit.read_bytes()) == size
        unit.write_bytes(bytes(256))
        other = root / "nrom.nes"
        h = bytearray(b"NES\x1a" + bytes(12)); h[4] = 1
        other.write_bytes(h + bytes(16384))
        for args in (["--barcode", "96385074"], ["--datach-save", unit]):
            result = run([other, *args, "--frames", 1])
            assert result.returncode != 0 and "157" in result.stderr, result.stderr
    print(f"CLI Datach: {launches} barcode launches, six idle launches, persistence and invalid inputs passed")


if __name__ == "__main__":
    main()
