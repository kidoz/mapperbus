# Memory allocation measurements

Measured 2026-11-12 on macOS 27.2.1, arm64, using the existing release build
(`-O3`, LTO, Apple Clang). Baseline: commit
`a7a5efaae2f50b41d9c446bc87d7d5cf8b28842d`. Candidate source/binary hashes,
fixture hashes and individual samples are in [the raw results](memory-measurements.json).
The unrelated `.gitignore` edit and untracked NodalKit checkout were excluded
from the change.

The operation measured is cartridge file loading, compared with initialization
and three frames of SDL GPU xBRZ at 6×. Acceptance targets were a reduction of
at least 14 MiB in the 15 MiB PRG-only loading stress case, one rather than two
allocations of at least 1 MiB, retained storage within the header/trainer's
528-byte allowance, and no additional large allocations in the mixed-ROM/GPU
controls. Content identity, region selection, banking, snapshots and ownership
must remain equivalent to the borrowed-span loader.

## Results

Each row has three fresh-process samples per subject. Order alternated between
baseline/candidate and candidate/baseline; file caches were warm. No test suite
ran alongside the final measurements. All C++ allocation counts were identical
across the three isolated repetitions.

| Workload | Baseline peak C++ bytes | Candidate peak C++ bytes | Difference |
|---|---:|---:|---:|
| 1 MiB PRG-only mapper-31 image | 2,138,312 | 1,089,736 | −1 MiB |
| 15 MiB PRG-only loading stress image | 31,498,440 | 15,769,800 | −15 MiB |
| J.Y. mapper 90, 2 MiB PRG + 2 MiB CHR | 8,388,864 | 8,388,864 | 0 |
| SDL GPU xBRZ, 256×240 at 6× | 8,847,360 | 8,847,360 | 0 |

The stress case's whole-process maximum RSS was 37.828 MiB before and
22.812–22.828 MiB after. This corroborates the allocation reduction, but is not
a general RSS guarantee. GPU RSS varied between processes; no GPU RSS or
throughput improvement is claimed. The GPU produced a texture in every run.
Its 8.438 MiB CPU fallback image remains a separate optimization candidate.

For the PRG-only cases, the number of requests of at least 1 MiB fell from two
to one. Retained C++ storage increased by just 16 bytes, the removed image
header's capacity. Trainer-bearing images may retain another 512 bytes.

**PASS:** the measured allocation targets. The 15 MiB input exercises the
existing loader's accepted-size behavior; it does not certify mapper 31 can
address that entire image. The native-size 1 MiB case verifies ordinary usage.
An earlier comparison overlapped verification work and was excluded from RSS
conclusions; its samples remain in the raw results. Its C++ byte counts agreed.

## Implementation boundary

`Cartridge::from_file()` can now transfer its owned buffer into a mapper when
the image contains only PRG plus the header/optional trainer, with exact-size
capacity. SHA-256 and legacy CRC32 are computed before removing that prefix.
Both PRG and CHR bounds are checked before allocating payload buffers.

The borrowed `from_data()` API still copies its input. Mixed PRG/CHR images and
files with trailing padding use the original payload-copy path, avoiding
retention of unrelated file capacity. Archive loading is unchanged. File caps,
mapper bank equations and save-state layouts are unchanged.

## Reproduce

Build the probe against the baseline and candidate libraries in separate build
trees, keeping the resulting executables separate:

```sh
ninja -C buildDir-release-arm64 src/core/libmapperbus-core.a src/frontends/sdl3/libmapperbus-sdl3.a
c++ -std=c++23 -O3 -flto -Isrc tools/probe_memory.cpp \
  buildDir-release-arm64/src/frontends/sdl3/libmapperbus-sdl3.a \
  buildDir-release-arm64/src/core/libmapperbus-core.a \
  $(pkg-config --cflags --libs sdl3) -o /tmp/mapperbus-memory-probe
/usr/bin/time -l /tmp/mapperbus-memory-probe rom /path/to/synthetic.nes
/usr/bin/time -l /tmp/mapperbus-memory-probe gpu
```

Generate the exact synthetic fixtures in a temporary directory:

```python
from pathlib import Path
import tempfile

root = Path(tempfile.mkdtemp(prefix="mapperbus-memory-fixtures-"))
for name, prg, chr_size, mapper in [
    ("native31", 1024 * 1024, 0, 31),
    ("stress31", 15 * 1024 * 1024, 0, 31),
    ("jy90", 2 * 1024 * 1024, 2 * 1024 * 1024, 90),
]:
    h = bytearray(b"NES\x1a" + bytes(12))
    p, c = prg // 16384, chr_size // 8192
    h[4], h[5] = p & 255, c & 255
    h[6], h[7] = (mapper & 15) << 4 | 1, (mapper & 240) | 8
    h[9], h[11] = (p >> 8) | ((c >> 8) << 4), 0 if chr_size else 7
    with (root / (name + ".nes")).open("wb") as f:
        f.write(h)
        for bank in range((prg + chr_size) // 4096):
            f.write(bytes([bank & 255]) * 4096)
print(root)
```

The standalone probe interposes C++ allocation/deallocation only in its own
executable. It counts requested live bytes, excluding its allocator bookkeeping,
direct C allocations, GPU textures and driver allocations. `/usr/bin/time -l`
reports process-wide maximum RSS in bytes on macOS. Timing and percentile
latency were not measured. The probe is diagnostic tooling, not a production
allocator or a new CI performance gate.

## Correctness validation

Eight combinations of legacy/NES 2.0 headers, trainers and trailing padding
compare owned-file and borrowed-image hashes, regions, all 256 mapper-31 pages,
CHR RAM and mapper snapshots. They also verify source files remain unchanged
and borrowed-input mutation cannot affect a loaded cartridge.

All 63 regular test targets passed with `MAPPERBUS_REQUIRE_GPU=1`, including
3,405 GPU assertions across seven cases and 20,444 ROM-source assertions across
38 cases. All eight focused ASan+UBSan targets passed serially, including the
ownership regressions in `rom-source`. Formatting, scoped clang-tidy, the
probe's warning-clean syntax check and whitespace checks passed.
