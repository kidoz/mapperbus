# Changelog

All notable changes to MapperBus are documented in this file.

The format is based on Keep a Changelog, and this project adheres to Semantic
Versioning.

## [Unreleased]

### Added

- Button-based gamepad assignment for Standard and Four Score, including only
  attached Standard expansion pads, with ordered roles and atomic save/application.

- Per-game controller hardware preferences shared by GUI, SDL3 and CLI, matched
  by content across raw/archive ROMs, with a Remember for this game switch,
  global fallback, CLI precedence and recoverable save errors.

- Hori four-controller assignment wizard using fresh button presses, duplicate
  detection, disconnect recovery, and atomic save/application with safe Cancel.

- Saved controller identities across reconnects and reordered startup in SDL3
  and GUI, with explicit reassignment for indistinguishable controllers.

- Hori Input settings preset to enable and assign four gamepads while retaining
  keyboard/button bindings and deadzones, with persistence and hotplug coverage.

- Opt-in Japanese Wit's ZIP regression for Hori signature detection, four
  independent player controls, built-in aliases, and multiplayer snapshot replay.

- Opt-in Japanese Bomberman II ZIP regression for independent three-player
  controls and snapshot replay in Standard and Hori modes.

- Hori 4 Players Adapter mode with independent built-in controllers, six host
  profiles, GUI/CLI/configuration support, shared serial/DMA timing, and
  version-19 snapshots with legacy import.

- Hash-pinned allpads controller detection and snapshot diagnostics for Standard
  and Four Score modes, using the author's unmodified public ROM.

- Four Score four-player mode with 24-bit reports and signatures, GUI/CLI/shared
  configuration selection, existing four-profile bindings, DMA timing, and
  version-18 snapshots with legacy import.

- Two independently attached standard Famicom expansion controllers with four
  keyboard/gamepad profiles, GUI attachment switches, SDL3/CLI overrides, shared
  serial/DMA timing, and version-17 snapshots with legacy import.

- Asynchronous macOS microphone permission requests, denied/restricted guidance,
  Retry/reopen recovery, and permission-loss checks before capture reads. Native
  prompts stay outside the core; status messages wrap and expose accessible text.

- Optional microphone audio playback with independent volume, SDL3 overrides,
  bounded host-only PCM mixing, and queue clearing across pause, snapshots,
  device failures, and settings changes. Playback starts off and leaves input
  detection and save-state format unchanged.

- Optional live Famicom microphone capture with device selection, sensitivity,
  retry/status controls, shared preferences, SDL3 overrides, and macOS purpose
  metadata. Pause and snapshot loads discard capture buffers; bound simulation
  remains available. Captured sound is neither saved nor mixed into audio.

- Six pinned AccuracyCoin DMA register/controller checks with synthetic
  decoder, continuous-read, strobe-phase, and snapshot regressions.

- Targeted hash-pinned AccuracyCoin explicit/implicit DMC abort diagnostics,
  with synthetic stop, duplicate-read, regional, and snapshot regressions.

- Ten optional hash-pinned NTSC PPU diagnostics, with synthetic race, frame-length,
  region, and snapshot regressions around the skipped dot.
- CPU interrupt regressions for I-flag latency, branch polling, NMI takeover of
  IRQ/BRK, interrupts during DMA, and five optional hash-pinned Blargg diagnostics.
- CPU/DMC/OAM timing regressions, including optional hash-pinned public Blargg
  DMA diagnostics, overlap at the last OAM transfers, and delayed-request snapshots.
- Independent FDS BIOS disk-write checks for fast/slow dumps, physical block
  CRCs, write protection, CRC-error retry, and snapshots during writes, using
  the pinned public BIOS dumper and fixed CRC vectors from FDSKey.
- End-to-end FDS game regressions through raw, ZIP, and 7z loading, including
  shared snapshots, cross-source progress reloads, and mixed-archive failure paths.
- FDS DRAM refresh watchdog with CPU/dummy/DMA address accounting, shared IRQ
  acknowledgement, and regression coverage against the public hardware diagnostic.
- Original FDS audio capture probe with 16 pages for multiplier, counter, reset,
  and envelope research; generated without BIOS/game data or an assembler.
- FDS BIOS boot with the RAM adapter, raw/archived disk loading, side and
  write-protection controls, persistent disk changes, and disk-image export.
- Shared FDS BIOS configuration and CLI `--fds-bios` / `--frames` options.
- Selectable NES/AV Famicom or original RF Famicom controller timing, with
  shared configuration, CLI/SDL3 overrides, and GUI hardware settings.
- Selectable RF Famicom controller-port wiring, including floating data bits and
  the six-button second controller, with shared settings and frontend overrides.
- Bindable digital Famicom microphone activity with M as the keyboard default,
  gamepad mappings, deterministic CPU-cycle pulses, and snapshot persistence.

### Fixed

- Four Score identification bit order, verified against raw serial positions
  and the independent allpads detection routine.

- Gamepad disconnects no longer adopt another connected device after SDL
  renumbers its device list; explicit device selection permits reassignment.

- NTSC APU register activation during DMA, `$4015` external-bus isolation,
  controller read-enable edges, and strobe sampling for NES/AV Famicom timing.
- Standard controllers shift in ones after eight bits. PPU I/O now uses its own
  latch, with palette read high bits and OAM attribute read masking.

- NTSC DMC stop-latch timing, one-cycle aborted DMA suppressed by CPU writes,
  and the late-2A03G/2A03H duplicate fetch when a load meets the byte clock.

- PAL CPU/PPU scheduling now advances 16 dots every five CPU cycles, including
  OAM/DMC DMA, with an integer divider phase around register accesses.

- Omit one dot on rendered odd NTSC frames, retaining the pre-render skip
  decision through late PPUMASK writes. PAL/Dendy retain full-length frames.
- Poll IRQ/NMI detector outputs at instruction-specific cycles, retain accepted
  interrupts through source acknowledgements and I-flag changes, and latch NMI
  during DMA. Clock the PPU around CPU accesses and delay APU frame-counter resets.
- DMC DMA now waits for eligible CPU reads, uses three/four-cycle get/put
  alignment, and overlaps setup with OAM DMA. APU/expansion audio advance once
  per bus cycle; the DMC output unit continues after the reader is disabled.
- FDS register acknowledgements on CPU dummy reads and DMA, with per-bus-cycle
  disk/timer clocks and bytes arriving during OAM/DMC stalls. Restore both NMOS
  read-modify-write writes, defer OAM DMA to the last page written, and preserve
  MMC1 consecutive-write filtering and reset handling.
- Keep the GUI window visible when FDS progress cannot be saved, using the
  toolkit's close-veto policy before its close notification.
- Resume the audio device when closing a paused game so the next game can
  advance instead of stalling behind an undrained audio queue.
- FDS audio phase advancement during wavetable writes, held output samples,
  volume-latch/envelope timing, fractional modulation pitch, and debug readback
  at `$4091` and `$4093–$4096` (with documented multiplier/counter approximations).
- FDS diagnostic boot bypasses that declare an absent final file, modulation-counter
  readback, and wavetable reads while playback is active.

- PPU NMI enable-edge handling and the vblank polling race used during FDS
  program reloads.

### Changed

- Save-state version 16 preserves microphone activity/phase, controller wiring
  and timing selections,
  line/strobe history, and PPU I/O latch, alongside DMC stop latches, regional
  clocks, interrupts, and FDS state. Versions 4–15 remain readable with
  reconstructed missing history;
  pre-v16 states import an inactive microphone; pre-v15 states select NES wiring;
  pre-v14 states select NES/AV Famicom timing;
  pre-v13 I/O latches start at zero and pre-v11 PAL states start with a fresh
  divider phase.

## [0.2.0] - 2026-11-04

### Added

- Turbo A and Turbo B input actions for keyboard and gamepad, with Slow, Normal,
  and Fast repeat speeds and explicit unbound bindings.
- Native macOS Settings window with a toolbar, persisted page selection, and
  accessible input controls; welcome screen with recent ROMs.
- NodalKit GUI frontend with a settings dialog, rebind flows, session actions,
  and a reusable input-binding table.
- MapperBus branding, application icons, and packaging for Linux, macOS, and
  Windows, including a macOS application-bundle target.
- Documentation organized around tutorials, how-to guides, reference, and
  explanation, with source-tree, authoring, and branding guides.
- Save-state slots with SDL3 hotkeys and GUI actions, and battery-backed `.sav`
  persistence.
- Two-player gamepad input, SDL3 window toggles, configurable keyboard and
  gamepad bindings, and gamepad command-line options.
- Mapper support for MMC2, MMC5 PRG/CHR banking with scanline IRQ, VRC6
  frequency control, VRC7 two-operator FM with ADSR, Sunsoft 5B noise and
  envelopes, and FDS disk-drive registers and audio.
- NES 2.0 header parsing with mapper extensions and submapper bits.
- CPU accuracy, mapper timing, APU regression, save-state, and GPU integration
  test coverage.
- Build and test CI workflows, Linux packaging checks, and embedded Vulkan
  shaders for the GPU upscalers.

### Changed

- Require NodalKit 0.3.1 and refresh the settings layout, input-binding table,
  and session shortcuts.
- Rework the APU output pipeline with per-stream filters, per-sample stereo,
  hardware-accurate filtering by default, a 96 kHz default sample rate, and
  dynamic rate control.
- Write save files atomically and validate save-state loads.

### Fixed

- Hold the triangle DAC output instead of gating it to zero.
- Correct CPU and APU timing for pulse timers, sweep, DMC, OAM DMA, and KIL.
- Fix MMC2 and MMC3 ROM indexing for non-power-of-two sizes and mask mapper
  fetch paths.
- Clear derived PPU state on reset and state load, and preserve NMI across
  `$2002` reads.
- Validate save states and FDS disk geometry, clamp battery loads, and flush
  battery RAM on cartridge switch and teardown.
- Restore fullscreen after SDL3 window recreation and keep fullscreen and vsync
  behavior consistent.
- Quote recent ROM paths in `mapperbus.conf` and prune entries that no longer
  open.

## [0.1.0] - 2026-03-30

### Added

- Initial release with the emulator core, SDL3 frontend, CPU and GPU FSR
  upscaling, and BlipBuffer-based audio output.

[0.2.0]: https://github.com/kidoz/mapperbus/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/kidoz/mapperbus/releases/tag/v0.1.0
