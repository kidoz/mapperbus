"""Original RAM-resident self-flashing programs, isolated raw-ROM CLI saves."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def flash_rom(mapper, legacy=False):
    header = bytearray(b"NES\x1a" + bytes(12))
    header[4] = 32
    header[6] = ((mapper & 15) << 4) | (2 if mapper == 30 else 0)
    header[7] = (mapper & 0xF0) | (0 if legacy else 8)
    if not legacy:
        header[11] = 9
    code = bytearray()
    def store(address, value):
        code.extend([0xA9, value, 0x8D, address & 255, address >> 8])
    if mapper == 30:
        for address, value in [(0xC000, 1), (0x9555, 0xAA), (0xC000, 0), (0xAAAA, 0x55),
                               (0xC000, 1), (0x9555, 0xA0), (0xC000, 2)]:
            store(address, value)
    else:
        for address, value in [(0x5000, 0), (0xD555, 0xAA), (0xAAAA, 0x55),
                               (0xD555, 0xA0), (0x5000, 1)]:
            store(address, value)
    store(0x8100, 0x5A)
    code.extend([0xAD, 0, 0x81, 0xC9, 0x5A, 0xD0, 0xF9, 0x85, 0x10])
    loop = 0x200 + len(code)
    code.extend([0x4C, loop & 255, loop >> 8])
    boot = bytes([0x78, 0xA2, 0, 0xBD, 0, 0xE0, 0x9D, 0, 2, 0xE8,
                  0xE0, len(code), 0xD0, 0xF5, 0x4C, 0, 2])
    prg = bytearray([0xFF]) * 524288
    for bank in range(0, len(prg), 32768):
        prg[bank+0x4000:bank+0x4000+len(boot)] = boot
        prg[bank+0x6000:bank+0x6000+len(code)] = code
        prg[bank+0x7FFA:bank+0x8000] = bytes([0, 0xC0] * 3)
    return header + prg


def main():
    binary = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="mapperbus-flash-cli-") as temp:
        root = Path(temp)
        environment = dict(os.environ, XDG_CONFIG_HOME=str(root / "config"))
        def run(source):
            return subprocess.run([str(binary), str(source), "--frames", "2"],
                                  capture_output=True, text=True, timeout=15,
                                  env=environment, check=False)
        for mapper in (30, 111):
            for legacy in (False, True):
                source = root / f"{mapper}-{legacy}.nes"
                original = flash_rom(mapper, legacy)
                source.write_bytes(original)
                for _ in range(2):
                    result = run(source)
                    assert result.returncode == 0, result.stderr
                    data = source.with_suffix(".sav").read_bytes()
                    assert len(data) == 524288 and data[0x8100] == 0x5A
                    assert source.read_bytes() == original
                for size in (524287, 524289):
                    save = source.with_suffix(".sav")
                    save.write_bytes(bytes(size))
                    result = run(source)
                    assert result.returncode != 0 and "flash save" in result.stderr.lower(), result.stderr
                    assert save.stat().st_size == size and source.read_bytes() == original
        for sub in (1, 2):
            source = root / f"readonly-{sub}.nes"
            image = flash_rom(30)
            image[6] &= ~2
            image[8] = sub << 4
            source.write_bytes(image)
            result = run(source)
            assert result.returncode == 0 and not source.with_suffix(".sav").exists(), result.stderr
    print("CLI flash: eight write/reload launches, eight corrupt saves and two read-only profiles passed")


if __name__ == "__main__":
    main()
