"""Original RAM-resident mapper-242 probes, including asymmetric ET-113 chips."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def rom(legacy=False, battery=False, et=False, pads=0):
    size = 655360 if et else 524288
    header = bytearray(b"NES\x1a" + bytes(12))
    header[4] = size // 16384
    header[6], header[7] = 0x21 | (2 if battery else 0), 0xF0 if legacy else 0xF8
    if not legacy:
        header[10], header[11] = 0x70 if battery else 0, 7
    code = bytearray([0x78])

    def store(address, value):
        code.extend([0xA9, value, 0x8D, address & 255, address >> 8])

    def expect(address, value):
        code.extend([0xAD, address & 255, address >> 8, 0xC9, value, 0xD0, 0xFE])

    def mapped(cpu, latch):
        pin = lambda n: (latch >> n) & 1
        a14 = (cpu >> 14) & 1 if pin(0) else pin(2)
        a15, a16 = pin(3), pin(4)
        if cpu >= 0xC000 and not battery and not pin(7):
            a14 = a15 = a16 = pin(9)
        result = (cpu & 16383) | a14 << 14 | a15 << 15 | a16 << 16
        if et and not pin(10):
            result += 524288
        else:
            result |= pin(5) << 17 | pin(6) << 18
        if pin(8):
            result = (result & ~31) | pads
        return ((result // 16384) * 37 + (result & 31)) & 255

    store(0x4017, 0x40)
    for latch in (0, 1, 0x201, 0x78, 0x279, 0x49C, 0x4BD, 0x7EF):
        store(0x8000 | latch, 0)
        for cpu in (0x9A07, 0xFA07):
            expect(cpu, mapped(cpu, latch))
    if battery:
        # The persistence signature is written only after every bank/pad test.
        store(0x6000, 0xA7)
        store(0x7FFF, 0x91)
    store(0x12, 0xC7)
    loop = 0x200 + len(code)
    code.extend([0x4C, loop & 255, loop >> 8])
    assert len(code) < 1024
    boot = bytearray()
    for page in range(4):
        boot.extend([0xA2, 0, 0xBD, 0, 0xE1 + page, 0x9D, 0, 2 + page, 0xE8, 0xD0, 0xF7])
    boot.extend([0x4C, 0, 2])
    prg = bytearray()
    for bank in range(size // 16384):
        data = bytearray(((bank * 37 + offset) & 255 for offset in range(32))) * 512
        data[0x2000:0x2000 + len(boot)] = boot
        data[0x2100:0x2100 + len(code)] = code
        data[-6:] = bytes([0, 0xE0] * 3)
        prg.extend(data)
    return header + prg


def main():
    binary = Path(sys.argv[1]).resolve()
    valid = invalid = 0
    with tempfile.TemporaryDirectory(prefix="mapperbus-242-cli-") as temp:
        root = Path(temp)
        env = dict(os.environ, XDG_CONFIG_HOME=str(root / "config"), XDG_DATA_HOME=str(root / "data"))

        def probe(data, profile="menu-0", accepted=True):
            nonlocal valid, invalid
            source = root / "probe.nes"
            save = source.with_suffix(".sav")
            save.unlink(missing_ok=True)
            source.write_bytes(data)
            result = subprocess.run([str(binary), str(source), "--board-profile", profile, "--frames", "2"],
                                    env=env, capture_output=True, text=True, timeout=15, check=False)
            if accepted:
                assert result.returncode == 0 and "Ran 2 frames successfully" in result.stderr, result.stderr
                valid += 1
                if data[6] & 2:
                    persisted = save.read_bytes()
                    assert len(persisted) == 8192 and persisted[0] == 0xA7 and persisted[-1] == 0x91
                else:
                    assert not save.exists()
            else:
                assert result.returncode != 0 and not save.exists(), result.stderr
                invalid += 1
            assert source.read_bytes() == data

        for legacy in (False, True):
            for battery, et in ((False, False), (True, False), (False, True)):
                for pads in (0, 31):
                    probe(rom(legacy, battery, et, pads), f"menu-{pads}")
        for pads in range(32):
            probe(rom(et=True, pads=pads), f"menu-{pads}")
        for legacy in (False, True):
            for offset, value in ((4, 0), (4, 16), (4, 31), (4, 33), (5, 1), (6, 0x28)):
                data = rom(legacy=legacy)
                data[offset] = value
                probe(data, accepted=False)
            probe(rom(legacy=legacy, battery=True, et=True), accepted=False)
        for offset, value in ((8, 0x10), (8, 0xF0), (10, 7), (10, 0x70), (11, 0), (11, 8), (11, 0x70)):
            data = rom()
            data[offset] = value
            probe(data, accepted=False)
        for profile in ("menu-", "menu--1", "menu-32", "menu-999999999999", "menu-1x", "unknown"):
            probe(rom(), profile, accepted=False)
    print(f"Mapper 242: {valid} loading profiles and {invalid} rejected inputs passed")


if __name__ == "__main__":
    main()
