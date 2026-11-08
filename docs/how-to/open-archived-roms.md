# Open archived ROMs

MapperBus can load `.nes` and `.fds` images directly from `.zip` and `.7z` archives through
libarchive. Install the dependency using the [build guide](build-and-install.md).
The examples below run from the repository root after building.

## Open an archive

In the NodalKit GUI, use **Open ROM** or drop an archive onto the window. If
several supported members exist, choose one in the **Open Archived ROM** dialog.
Recent games remember both the archive and the chosen member.

For an archive containing exactly one ROM, pass its path to SDL3 or the CLI:

```sh
./buildDir/src/app/mapperbus-sdl3 "/path/to/game.zip"
./buildDir/src/frontends/cli/mapperbus-cli "/path/to/game.7z"
```

The CLI runs 60 frames and exits. If an archive contains several ROMs, it prints
their member names and requires an exact, case-sensitive selection:

```sh
./buildDir/src/frontends/cli/mapperbus-cli "/path/to/collection.zip" --entry "folder/Game (E).nes"
./buildDir/src/app/mapperbus-sdl3 --entry "folder/Game (E).nes" "/path/to/collection.zip"
```

Archive extensions and member `.nes`/`.fds` extensions are case-insensitive. Region
detection uses the selected ROM's header, CRC database, then member filename.
Renaming the outer archive does not change the ROM's content identity.

## Find saves

Save states use the existing content-identified storage. Portable mode places
the state root beside the archive. Archived ROMs never import legacy sibling
`.state` files from beside the archive.

Battery saves for archived cartridges use `battery/<ROM-SHA256>.sav` under:

- macOS: `~/Library/Application Support/MapperBus/`
- Linux: `$XDG_DATA_HOME/mapperbus/`, or `~/.local/share/mapperbus/`
- Windows: `%APPDATA%/MapperBus/`

Raw ROMs continue using sibling `.sav` files. Moving a raw ROM's existing battery
save into this content-based location is a manual operation. See
[Save and restore a game](save-and-restore.md).

FDS disk changes use `fds/<normalized-disk-SHA256>.fds-save` under the same
application data directory. Raw, headered, ZIP, and 7z copies of the same disk
share progress. With the same BIOS, they also share identified snapshot slots.
The archive is preserved; game writes go to the separate disk-save file. See
[FDS setup and disk controls](play-fds-games.md).

The opt-in [FDS compatibility suite](../reference/fds-compatibility.md) exercises
Metroid and SMB2 through the shared loader in raw, ZIP, and 7z forms, including
snapshot interchange and Metroid progress reloads across formats.

## Supported scope and limits

Only ZIP and 7z containers with regular `.nes` or `.fds` members are exposed. Directories,
links, and other file types are ignored. Nested archives, encrypted archives,
and duplicate ROM member names are rejected or are not ROM candidates. FDS images additionally require an [FDS BIOS](play-fds-games.md).

The archive file and total declared member sizes are each limited to 256 MiB;
there may be at most 1024 entries. Member names are limited to 4096 bytes each
and 1 MiB combined. Selected ROM output is limited to 16 MiB, both before and
during decompression. Corrupt selected data and archive read errors abort the
load, preserving the previous game. These checks bound application buffers;
libarchive also allocates decoder memory internally, especially for solid 7z
archives. The GUI scans archives and prepares raw or archived media in the
background. A loading sheet shows the selected filename and offers Cancel.
It reports entries scanned, bytes decoded while loading the selected ROM, and
the final checking phase. The indicator remains indeterminate because scanning,
decompression, BIOS checks, and save loading do not share a reliable total.
Cancelling or failing to load keeps the current game and its previous pause state.
The outgoing game is saved first; that save step cannot be cancelled. If saving
fails, the current game stays available for recovery. This also ensures reopening
the same game reads its latest battery or disk save.

Cancel signals the reader to stop at the next checkpoint: before or after an
archive entry, a 64 KiB selected-data read, or a skip operation. Skipping unselected
entries retains libarchive's fast skip path. A libarchive or OS call already in
progress must return before the worker can stop and release its capacity.
Its eventual result is discarded, and it cannot replace the game or write the
staged game's saves. Up to four outstanding file operations are allowed; further
opens report that earlier operations are still waiting. Closing the app requests
the same stop without waiting for abandoned reads.

Entries are read into memory and never extracted to disk. Battery and state
paths are computed independently of archive member paths.

[How-to guides](README.md)
