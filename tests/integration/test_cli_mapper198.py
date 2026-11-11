"""Original mapper-198 CPU checks and physical NVRAM save/reload validation."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def rom(legacy=False, vertical=True):
    header = bytearray(b"NES\x1a" + bytes(12))
    header[4] = 40
    header[6], header[7] = 0x62 | int(vertical), 0xC0 if legacy else 0xC8
    if not legacy:
        header[10], header[11] = 0x76, 7
    prg = bytearray().join(bytes([page]) * 8192 for page in range(80))
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

    bank(6, 0x40)
    bank(7, 0x4D)
    expect(0x9A00, 0x40)
    expect(0xBA00, 0x4D)
    expect(0xDA00, 0x4E)
    expect(0xFA00, 0x4F)
    bank(6, 0x40, 0x40)
    expect(0x9A00, 0x4E)
    expect(0xDA00, 0x40)
    for raw, expected in ((0x50, 0x40), (0x7F, 0x4F), (0x80, 0), (0xBF, 0x3F), (0xFF, 0x4F)):
        bank(6, raw, 0x40)
        expect(0xDA00, expected)
    for page in range(8):
        bank(2, page)
        pwrite(0x1000, 0x70 + page)
    bank(0, 3)
    pread(0, 0x72)
    pread(0x400, 0x73)
    bank(1, 5)
    pread(0x800, 0x74)
    pread(0xC00, 0x75)
    bank(2, 0xFE, 0x80)
    pread(0, 0x76)
    pwrite(0, 0xA7)
    pread(0x1000, 0x72)
    store(0xA001, 0x80)
    store(0x6000, 0x59)
    for gate in (0, 0xC0):
        store(0xA001, gate)
        store(0x6000, 0)
        store(0x5000, gate ^ 0x39)
        store(0x5FFF, gate ^ 0x73)
        expect(0x5000, gate ^ 0x39)
        expect(0x5FFF, gate ^ 0x73)
        pwrite(0, gate ^ 0xA7)
        pread(0, gate ^ 0xA7)
        if gate == 0xC0:
            expect(0x6000, 0x59)
        store(0xA001, 0x80)
        expect(0x6000, 0x59)
    store(0x6000, 0xA7)  # Reached only after the CPU banking and RAM checks.
    loop = 0xE000 + len(code)
    code.extend([0x4C, loop & 255, loop >> 8])
    assert len(code) < 0x1800
    prg[0x9E000:0x9E000 + len(code)] = code
    prg[0x9FFFA:0xA0000] = bytes([0, 0xE0] * 3)
    return header + prg


def main():
    binary = Path(sys.argv[1]).resolve()
    valid_count = invalid_count = 0
    with tempfile.TemporaryDirectory(prefix="mapperbus-198-cli-") as temp:
        root = Path(temp)
        env = dict(os.environ, XDG_CONFIG_HOME=str(root / "config"),
                   XDG_DATA_HOME=str(root / "data"))

        def run(source):
            return subprocess.run([str(binary), str(source), "--frames", "2"],
                                  env=env, capture_output=True, text=True,
                                  timeout=15, check=False)

        for legacy in (False, True):
            for vertical in (False, True):
                source = root / f"198-{legacy}-{vertical}.nes"
                original = rom(legacy, vertical)
                source.write_bytes(original)
                result = run(source)
                assert result.returncode == 0 and "Ran 2 frames successfully" in result.stderr, result.stderr
                save = source.with_suffix(".sav")
                assert save.exists() and len(save.read_bytes()) == 8192
                assert save.read_bytes()[0] == 0xA7, (source.name, "CPU checks did not complete")
                payload = bytearray(save.read_bytes())
                payload[0], payload[-1] = 0x11, 0x69
                save.write_bytes(payload)
                result = run(source)
                assert result.returncode == 0, result.stderr
                payload[0] = 0xA7
                assert save.read_bytes() == payload, (source.name, "save reload/CPU checks failed")
                assert source.read_bytes() == original
                valid_count += 1
        changes = [(8, 0x10), (6, 0x61), (10, 0x70), (10, 0x77), (10, 0x75),
                   (10, 6), (10, 0x86), (10, 0x66), (11, 0), (11, 6), (11, 8),
                   (11, 0x70), (6, 0x6B), (4, 39)]
        for index, (offset, value) in enumerate(changes):
            bad = rom()
            bad[offset] = value
            source = root / f"invalid-198-{index}.nes"
            source.write_bytes(bad)
            assert run(source).returncode != 0
            assert source.read_bytes() == bad and not source.with_suffix(".sav").exists()
            invalid_count += 1
        for kind in ("legacy-battery", "large-prg", "chr-rom"):
            bad = rom(legacy=kind == "legacy-battery")
            if kind == "legacy-battery":
                bad[6] &= ~2
            elif kind == "large-prg":
                bad[4] = 41
                bad.extend(bytes(16384))
            else:
                bad[5] = 1
                bad.extend(bytes(8192))
            source = root / f"invalid-198-{kind}.nes"
            source.write_bytes(bad)
            assert run(source).returncode != 0
            assert source.read_bytes() == bad and not source.with_suffix(".sav").exists()
            invalid_count += 1
    print(f"CLI mapper 198: {valid_count} CPU-verified save/reload profiles and {invalid_count} invalid headers passed")


if __name__ == "__main__":
    main()
