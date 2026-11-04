# Changelog

All notable changes to MapperBus are documented in this file.

The format is based on Keep a Changelog, and this project adheres to Semantic
Versioning.

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
