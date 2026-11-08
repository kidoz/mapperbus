# Play Famicom Disk System games

Use an 8 KiB FDS BIOS and a headered or headerless `.fds` image. The BIOS and
commercial games are supplied separately. Run the commands from the repository
root after [building MapperBus](build-and-install.md).

## Select the BIOS and open a disk

In the NodalKit GUI, open **Settings → General → BIOS**, click **Choose BIOS…**,
and select the BIOS file. Then use **Open ROM** to open a `.fds` image.
The BIOS path is remembered in the shared configuration. Changing the BIOS
selection takes effect the next time a disk is opened.

If opening fails because the BIOS is missing, unreadable, or the wrong size,
the error dialog also offers **Choose BIOS…**. MapperBus checks the selected file
in the background, saves its path, then retries the same disk and archive member.
Cancel or a validation failure keeps the current game and previous BIOS selection.
A settings-save failure offers **Retry** or **Keep Current Settings**; keeping
the settings returns to disk recovery. The current game's pause state is preserved
until a replacement loads successfully. In a session started with **Continue
Without Saving**, the new BIOS path applies only for that session.

For SDL3, pass the BIOS explicitly or use the saved path:

```sh
./buildDir/src/app/mapperbus-sdl3 --fds-bios "/path/to/disksys.rom" "/path/to/game.fds"
```

The headless CLI can run a longer boot check without opening a window:

```sh
./buildDir/src/frontends/cli/mapperbus-cli "/path/to/game.fds" --fds-bios "/path/to/disksys.rom" --frames 1200
```

If no BIOS path is configured, MapperBus looks for `bios/disksys.rom` under the
[application data directory](open-archived-roms.md#find-saves). A BIOS must contain
exactly 8192 bytes. An invalid BIOS or disk leaves the current game loaded.

Disks can also be [opened from ZIP or 7z archives](open-archived-roms.md). Use
`--entry "folder/Game.fds"` when an archive contains several games.

## Change disk sides

| Key in SDL3 or the GUI | Action |
|---|---|
| `F6` | Insert the next side, cycling through the image |
| `Shift+F6` | Eject the disk, or reinsert side A |
| `F8` | Toggle write protection |

The GUI's **Emulation → Disk Controls…** dialog also lets you select any side,
eject, change write protection, and export a modified disk. The status bar shows
the selected disk/side, drive activity, write protection, and unsaved changes.

Wait for the game's side-change prompt before switching. A side change briefly
reports the disk as removed so software can detect reinsertion. If you switched
too early, eject, let the game observe the removal, then insert the requested
side. Multi-side images label their sides Disk 1 A, Disk 1 B, Disk 2 A, and so on.

## Keep disk progress

Disks open writable. Game writes are saved separately under
`fds/<original-disk-SHA256>.fds-save` in the application data directory. The hash
uses raw side bytes without an optional `.fds` header, so raw and archived
copies share disk progress. The original image or archive is preserved.

MapperBus saves changes periodically when the drive is idle. In the NodalKit GUI,
disk writes run in the background while play continues; side changes also queue a
save. Newer changes remain pending until their own save succeeds. Disk Controls
shows save status and offers **Retry Disk Save** after a failure. Fix the storage
problem, then retry or export the disk to recover progress.

State saves, game switches, **Close ROM**, and normal Quit wait for existing disk
writes before saving current progress. The GUI shows **Saving Progress** while
closing; a save failure keeps the game open with **Keep Open** and **Retry**.
SDL3 and CLI retain synchronous disk saves. Do not interrupt an in-game disk
write or force-quit while persistence is pending.

Use **Export Modified Disk…** to write a headerless `.fds` copy containing the
current changes. Choose a new destination. The GUI captures the disk when export
starts, then checks the destination and writes in the background. The progress
sheet stays open until writing finishes; Escape does not cancel a started export.
Quit waits for export before saving progress and closing. A failed export keeps
the game open and shows the error and destination; choose **Export Modified
Disk…** again to retry or select another path. An export does not mark automatic
disk persistence as saved, and later game writes are outside the captured copy.

The original disk or containing archive cannot be the export destination,
including symbolic or hard links to that file. Writes use an exclusively created
temporary sibling and replace the destination only after writing succeeds.
The `.fds-save` file is MapperBus's versioned save format, not a disk image for
other emulators.

[Save states](save-and-restore.md) include disk contents, adapter RAM, side,
transfer position, and audio state. Loading one can restore older disk progress;
the next disk-save flush persists that restored progress. States require the
same original disk and BIOS bytes.

## Supported images

The loader accepts whole 65500-byte sides, with an optional 16-byte fwNES header,
up to 16 sides. Standard blocks 1–4, hidden files, reconstructed gaps, and CRCs
are supported. Truncated or malformed file layouts are rejected. QD, raw flux,
copy-protected/custom track formats, and analog drive/DRAM effects are outside
this implementation's scope. Compatibility is not guaranteed for every title.

[How-to guides](README.md)
