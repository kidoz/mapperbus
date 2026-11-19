"""Boot the fitted New Star 128-in-1 and mapper 225 populations."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def rom(legacy=False, vertical=True, large=False, new_star=False):
    header = bytearray(b"NES\x1a" + bytes(12))
    header[4], header[5] = (128, 128) if large else (2, 1)
    if new_star:
        header[4], header[5] = 64, 64
    header[6], header[7] = 0x10 | int(vertical), 0xE0 if legacy else 0xE8
    if new_star:
        header[6] |= 0xE0
        header[7] |= 0x10
    prg = bytearray(header[4] * 16384)
    for bank in range(header[4]):
        base = bank * 16384
        prg[base + 0x2000:base + 0x2004] = bytes([0x78, 0x4C, 1, 0xE0])
        prg[base + 0x3FFA:base + 0x4000] = bytes([0, 0xE0] * 3)
    return header + prg + bytes(header[5] * 8192)


def main():
    binary = Path(sys.argv[1]).resolve()
    accepted = rejected = 0
    with tempfile.TemporaryDirectory(prefix="mapperbus-255-cli-") as temp:
        root = Path(temp)
        env = dict(os.environ, XDG_CONFIG_HOME=str(root / "config"),
                   XDG_DATA_HOME=str(root / "data"))

        def probe(data, unsupported=False):
            source = root / "probe.nes"
            source.write_bytes(data)
            result = subprocess.run([str(binary), str(source), "--frames", "2"],
                                    env=env, capture_output=True, text=True,
                                    timeout=15, check=False)
            if unsupported:
                assert result.returncode != 0 and "Unsupported discrete PRG/CHR ROM population" in result.stderr, result.stderr
            else:
                assert result.returncode == 0 and "Ran 2 frames successfully" in result.stderr, result.stderr
            assert source.read_bytes() == data
            assert not source.with_suffix(".sav").exists()

        for legacy in (False, True):
            for vertical in (False, True):
                probe(rom(legacy, vertical, new_star=True))
                for large in (False, True):
                    data = rom(legacy, vertical, large)
                    probe(data)
                    accepted += 1
                    data[6] |= 0xE0
                    data[7] |= 0x10
                    for battery in (False, True):
                        bad = bytearray(data)
                        if battery:
                            bad[6] |= 2
                            if not legacy:
                                bad[10] = 0x70
                        probe(bad, unsupported=True)
                        rejected += 1
    print(f"Mapper 255: {rejected} unsupported populations rejected, four New Star images booted; mapper 225: {accepted} boot profiles passed")


if __name__ == "__main__":
    main()
