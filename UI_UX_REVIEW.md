# MapperBus GUI — UI/UX Review & Fix Log (Linux / GNOME)

Date: 2026-09-15 · Scope: NodalKit GUI frontend (`src/frontends/nodalkit/`) and the
NodalKit toolkit (`subprojects/nodalkit`) · Reference design:
`design_handoff_nodalkit_platform_themes/README.md`

Method: reviewed the running app on a GNOME 50 Wayland session (light + dark),
drove it with real input, and verified every state with in-app widget-tree dumps
and software-rendered captures (`WindowInspector::save_debug_bundle`).
Screenshots: `~/.cache/mb-ui-review/captures/`.

---

## 1. Overall assessment

The GUI is a convincing Adwaita-style shell: client-side headerbar with centered
title/subtitle, flat Open… button, hamburger primary menu, boxed-list preference
groups, sheet dialogs, toasts, and a status bar matching the handoff composition.
Dark and light both track the system scheme. All menu actions and all three
settings tabs function and persist correctly.

The review found four systemic defects (all fixed, see §3): dead mouse-wheel
scrolling in sheets, the hidden closed menu participating in keyboard focus,
modal sheets not owning the keyboard, and preference rows whose titles collapsed
into ellipses.

## 2. What was already working

- Headerbar/title/subtitle, context-aware hamburger menu (Pause ⇄ Resume,
  emulation items only with a ROM loaded).
- Settings round-trip: density, scale, and the full audio chain persist to
  `mapperbus.conf` and apply live.
- Rebinding end-to-end: capture dialog, conflict-swap confirm, toast, table
  refresh, persistence. Disabled Pad buttons skipped in Tab order.
- Open ROM via the native portal dialog; About; Quit (config saved on close).
- Accessibility plumbing: AT-SPI subtree, accessible names on menu controls.
- Status bar composition and nominal FPS per region; window/task-switcher title
  split (clean media title in the headerbar, file-backed title for the shell).

## 3. Findings and fixes

### P0 — Mouse wheel did not scroll the Settings sheet

Root cause: Mutter delivers wheel clicks as `wl_pointer.axis_value120` with a
zero smooth `axis` value; NodalKit implemented only the smooth handler, so every
wheel click was a no-op (only touchpad scrolling could ever work). The bindings
table and lower Audio rows were unreachable by wheel.

Fix: `WaylandInput::pointer_axis_value120` now translates each 120 step into one
discrete `MouseEvent::Type::Scroll` (`wayland_input.cpp`).
Verified live: the Input page scrolls and the bindings table becomes visible.

### P0 — Closed context menu sat in the Tab chain

The menu widget stayed mapped while closed (only its paint checked `open`), so
Tab from the game surface landed on an invisible widget, and Enter two stops
later could hit the headerbar Close — quitting the app. Focus also stayed on the
hidden menu after Escape.

Fix: `ContextMenu` now maps itself in `show_at()` and unmaps in `dismiss()`
(unmapping also releases focus via the host window), starts unmapped in the
constructor, and gained `on_dismissed()` (not emitted for item activation, via a
private `dismiss_without_activation()`). MapperBus connects `on_dismissed` to
return focus to the game surface. Outside-click dismissal already existed.

### P1 — Modal sheets did not own the keyboard

Two parts, both fixed in `window.cpp`:

1. `show_overlay(modal)` now moves focus into the sheet (first focusable
   descendant), so Tab/Return/Escape operate on the dialog; existing
   `previous_focus` restore returns focus on dismiss. Verified: focus ring lands
   on the settings tab strip when the sheet opens.
2. `key_state` (the polled path game input reads) is no longer updated while a
   modal overlay is visible and is cleared instead, so the emulator stops
   reacting to keys behind an open dialog.

### P1 — Preference row titles truncated

The Settings page stacked 28 px clamp + 32 px slot insets per side (60 px), and
`PreferencesRow` gave suffix controls their full natural width, starving titles
("Chann…", "Dead…", "UI De…"), clipping "Pseudo Stereo" and "Gamepad N (not
connected)".

Fix: insets reduced to the clamp's 20 px (page slot keeps 16 px top), and
`PreferencesRow::allocate` caps suffixes at 60 % of the row width — segmented
controls compress by design, combos fit. Verified: "Deadzone" renders in full
with a 299 px control in a 499 px row.

### P1 — Dimmed values were not dim

`SecondaryText` resolved the generic `text-color` rule (which lands on
`text-primary` for unclassed widgets) and `ValueText` rendered 17 pt Medium.
Fix: `SecondaryText` uses `text-secondary`; `ValueText` renders 13.5 pt Regular
so row values read as supporting information (still `text-secondary` when
dimmed).

### P2 — assorted fixes in `gui_controller.cpp`

- Tab switching preserved the previous page's scroll offset; pages now start at
  the top (offset restore only within the same page, tracked by
  `settings_built_page_`).
- Toast on load dropped the hardcoded controls hint (wrong after rebinding, and
  truncated anyway): now "Loaded ⟨title>.".
- Conflict dialog copy rewritten to be unambiguous: "⟨key> is already used by
  the ⟨button> button. Replace swaps them: ⟨button> moves to ⟨previous key>."
  (keyboard and gamepad paths).
- About dialog: the `\n` in the body rendered as a tofu box (Label cannot emit
  newlines); body is now a single line. (Version/copyright still to add.)
- "Keyboard — Ready" filler row removed; when gamepad support is missing the
  empty Controller group is not shown; Preferred-device and Deadzone controls
  are insensitive unless a device is connected.
- Native menu label unified to "Open ROM…" (Unicode ellipsis).

### Adaptation required by upstream

Upstream nodalkit (cd8bad6) removed `StatusBar::set_detail`; MapperBus now folds
the mapper detail into the segments ("Running | NTSC | 60.10 FPS | Mapper 7 ·
AxROM"). The right-aligned dim styling from the handoff is lost with the removed
API — revisit when the status bar API settles.

## 4. Corrections to the original review

- "Hamburger menu not keyboard-operable": re-tested with guaranteed window
  focus — arrow-key hover does move and render, and the handler receives keys.
  The earlier failures were focus races with other windows on the test desktop,
  not an app defect. The Escape path and outside-click dismissal work.
- The mystery app exits during testing were clean `close_requested` shutdowns
  (config file mtime + journal), not crashes. One real crash existed in the
  debug-only bundle renderer (`Widget::is_visible` on a stale pointer);
  debug-only, worth a hardening pass.

## 5. Handoff conformance notes

- Metrics and radii match the handoff exactly (36/30/28; 10/12/12, field 8);
  helper line under tabs, boxed lists, and status bar composition match.
- Color tokens deliberately diverge from the handoff tables (dark palette
  `#222226`-family vs the handoff's `#242428` charcoal; dark accent
  `#78AEED` vs `#6FA8F0`; dark `accent-contrast` white vs `#14161C`). The code
  comments ("track libadwaita named colors") contradict the handoff's
  high-fidelity tables while `IMPLEMENTATION_NOTES.md` claims exact application.
  Reconcile one way or the other; if the handoff's lighter dark accent is ever
  adopted, `accent-contrast` must flip to near-black in dark.
- Not implemented from the handoff canvas: Open-Recent dialog, disabled Latency
  row (fine to defer; record the decision).
- GNOME HIG follow-ups: ~~fullscreen (F11)~~ — **closed**: F11 toggles
  fullscreen via `nk::Window::set_fullscreen`; `on_window_state_changed` hides
  the headerbar/status bar and raises an OSD toast on every confirmed state
  change (app- or compositor-initiated alike); the hamburger menu carries a
  Fullscreen/Leave Fullscreen entry. Still open: accelerator column/global
  hotkeys for Pause/Reset/Save/Load State (blocked on the nodalkit `nk/actions`
  shortcut registry — declaring `ShortcutBinding{F11 → "win.toggle-fullscreen"}`-style
  accelerators beats the hand-rolled F11 check once it lands), and About
  version/copyright.

## 6. Verification

- `meson compile -C buildDir mapperbus-gui` — clean (0 errors).
- `just test` — 59 OK, 2 expected fail, 0 fail (includes nodalkit suites).
- `just format-check` — clean.
- Live checks with the fixes built in: wheel scroll through the whole Input
  page to the bindings table; focus ring on the settings tab strip on open;
  "Deadzone" untruncated; insensitive Preferred-device/Deadzone/Pad controls
  with no gamepad; 4-segment status bar; menu hover highlight rendering.
- Quality gates run after every change round; capture tooling and the temporary
  debug-action hook were removed from `main.cpp` / `gui_controller.*` before
  handoff (uncommitted-instrumentation rule).

## 7. Change inventory

MapperBus (`src/frontends/nodalkit/`): `gui_controller.cpp`, `gui_controller.hpp`
(new `settings_built_page_`), `gui_widgets.hpp` (SecondaryText token, ValueText
font).
NodalKit (`subprojects/nodalkit/`): `src/platform/linux/wayland_input.{h,cpp}`
(value120 wheel), `src/platform/window.cpp` (modal key_state gating, modal focus
grab), `src/widgets/context_menu.{h,cpp}` (mapping, `on_dismissed`,
`dismiss_without_activation`), `src/widgets/preferences.cpp` (suffix cap).

Commit note: nodalkit changes are uncommitted in the submodule — commit them
promptly; a working-tree reset during this session briefly wiped them and they
were re-applied. MapperBus changes are limited to the three files above
(`.gitignore`, `.agents/**` and tool adapters untouched).
