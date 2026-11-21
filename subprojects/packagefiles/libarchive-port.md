# libarchive Meson build port

libarchive upstream ships CMake and autotools builds. `packagefiles/libarchive/`
carries the Meson build overlaid by `subprojects/libarchive.wrap` on the
libarchive 3.8.11 source tree.

- The port is derived from the WrapDB `libarchive_3.8.9-1` Meson packaging
  (https://wrapdb.mesonbuild.com/v2/libarchive_3.8.9-1/libarchive.wrap).
- Changes for 3.8.11: the project version, the new `archive_crc32.c`,
  `archive_umask.c`, `lafe_explicit_bzero.c` and `lafe_setmode.c` sources, the
  new `ARCHIVE_CRYPTO_WIN` CNG define, feature probes for the new
  `explicit_bzero`, `memset_explicit`, `waitpid` and `sys/single_threaded.h`
  checks, and the test source list regenerated from
  `libarchive/test/CMakeLists.txt`.
- `test_utils/mklist.py` is kept from the WrapDB port to generate `list.h`.
- Keep this README outside `packagefiles/libarchive/`: `patch_directory` copies
  every file in that directory over the upstream source tree.

When WrapDB publishes a 3.8.11 or newer wrap, prefer replacing this port with
the WrapDB files.
