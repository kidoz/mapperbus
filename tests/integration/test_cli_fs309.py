"""Original FS309 CPU programs and whole-chip SRAM save/reload checks."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def rom(legacy=False, vertical=True, prg_size=1048576):
    header = bytearray(b"NES\x1a" + bytes(12))
    header[4] = prg_size // 16384
    header[6], header[7] = 0x72 | int(vertical), 0xC0 if legacy else 0xC8
    if not legacy:
        header[10], header[11] = 0x90, 7
    count = prg_size // 8192
    prg = bytearray().join(bytes([page]) * 8192 for page in range(count))
    code = bytearray([0x78])

    def store(address, value):
        code.extend([0xA9, value, 0x8D, address & 255, address >> 8])

    def expect(address, value):
        code.extend([0xAD, address & 255, address >> 8, 0xC9, value, 0xD0, 0xFE])

    def bank(reg, value, mode=0):
        store(0x8000, reg | mode)
        store(0x8001, value)

    def address(value):
        store(0x2006, value >> 8)
        store(0x2006, value & 255)

    def pwrite(where, value):
        address(where)
        store(0x2007, value)

    def pread(where, value):
        address(where)
        code.extend([0xAD, 7, 0x20])
        expect(0x2007, value)

    bank(6, 0x50)
    bank(7, 0x7D)
    expect(0x9A00, 0x50 % count)
    expect(0xBA00, 0x7D % count)
    expect(0xDA00, count - 2)
    expect(0xFA00, count - 1)
    bank(6, 0x50, 0x40)
    expect(0x9A00, count - 2)
    expect(0xDA00, 0x50 % count)
    bank(6, 0xFF, 0x40)
    expect(0xDA00, 0x7F % count)
    bank(6, 0x80, 0x40)
    expect(0xDA00, 0)
    locations = ((0, 0x31), (0x400, 0x73), (0x1000, 0x59), (0x1FFF, 0xA7))
    for where, value in locations:
        pwrite(where, value)
    for mode in (0, 0x80):
        for reg, value in enumerate((0x6B, 0xF1, 3, 0x7D, 0x99, 0xAB)):
            bank(reg, value, mode)
        for where, value in locations:
            pread(where, value)
    store(0xA001, 0x80)
    store(0x6000, 0x59)
    for gate in (0, 0xC0):
        store(0xA001, gate)
        store(0x6000, 0)
        store(0x5000, gate ^ 0x39)
        store(0x5FFF, gate ^ 0x73)
        expect(0x5000, gate ^ 0x39)
        expect(0x5FFF, gate ^ 0x73)
        pwrite(0x400, gate ^ 0xA7)
        pread(0x400, gate ^ 0xA7)
        if gate == 0xC0:
            expect(0x6000, 0x59)
        store(0xA001, 0x80)
        expect(0x6000, 0x59)
    store(0x5000, 0xA7)  # Persistent success marker reached after all CPU checks.
    loop = 0xE000 + len(code)
    code.extend([0x4C, loop & 255, loop >> 8])
    assert len(code) < 0x1800
    prg[-8192:-8192 + len(code)] = code
    prg[-6:] = bytes([0, 0xE0] * 3)
    return header + prg


def main():
    binary = Path(sys.argv[1]).resolve()
    valid_count = invalid_count = 0
    with tempfile.TemporaryDirectory(prefix="mapperbus-fs309-cli-") as temp:
        root = Path(temp)
        env = dict(os.environ, XDG_CONFIG_HOME=str(root / "config"),
                   XDG_DATA_HOME=str(root / "data"))

        def run(source):
            return subprocess.run([str(binary), str(source), "--frames", "2"],
                                  env=env, capture_output=True, text=True,
                                  timeout=15, check=False)

        for legacy in (False, True):
            for vertical in (False, True):
                for p in (32768, 98304, 524288, 655360, 1048576):
                    source = root / f"fs309-{legacy}-{vertical}-{p}.nes"
                    original = rom(legacy, vertical, p)
                    source.write_bytes(original)
                    result = run(source)
                    assert result.returncode == 0 and "Ran 2 frames successfully" in result.stderr, result.stderr
                    save = source.with_suffix(".sav")
                    assert save.exists() and len(save.read_bytes()) == 32768
                    assert save.read_bytes()[0x5000] == 0xA7, (source.name, "CPU checks did not complete")
                    assert save.read_bytes()[0x6000] == 0x59
                    assert save.read_bytes()[0x5FFF] == 0xB3
                    payload = bytearray(save.read_bytes())
                    payload[0], payload[0x5000], payload[0x5FFE], payload[-1] = 0xC9, 0x11, 0xD9, 0x69
                    save.write_bytes(payload)
                    result = run(source)
                    assert result.returncode == 0, result.stderr
                    payload[0x5000] = 0xA7
                    assert save.read_bytes() == payload, (source.name, "whole-chip save/reload failed")
                    assert source.read_bytes() == original
                    valid_count += 1
        changes = [(8, 0x10), (6, 0x71), (10, 0), (10, 0x70), (10, 0x80),
                   (10, 0xA0), (10, 0x96), (10, 6), (11, 0), (11, 6), (11, 8),
                   (11, 0x70), (6, 0x7B), (4, 1)]
        for index, (offset, value) in enumerate(changes):
            bad = rom()
            bad[offset] = value
            source = root / f"invalid-fs309-{index}.nes"
            source.write_bytes(bad)
            assert run(source).returncode != 0
            assert source.read_bytes() == bad and not source.with_suffix(".sav").exists()
            invalid_count += 1
        for kind in ("legacy-battery", "large-prg", "chr-rom"):
            bad = rom(legacy=kind == "legacy-battery")
            if kind == "legacy-battery":
                bad[6] &= ~2
            elif kind == "large-prg":
                bad[4] = 65
                bad.extend(bytes(16384))
            else:
                bad[5] = 1
                bad.extend(bytes(8192))
            source = root / f"invalid-fs309-{kind}.nes"
            source.write_bytes(bad)
            assert run(source).returncode != 0
            assert source.read_bytes() == bad and not source.with_suffix(".sav").exists()
            invalid_count += 1
    print(f"CLI FS309: {valid_count} CPU-verified 32 KiB save/reload profiles and {invalid_count} invalid headers passed")


if __name__ == "__main__":
    main()
