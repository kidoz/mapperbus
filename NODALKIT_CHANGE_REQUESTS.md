# NodalKit change requests — from the MapperBus UI/UX pass

Source: UI/UX review of the MapperBus GUI on Linux/GNOME (see
`UI_UX_REVIEW.md`). Base commit: **nodalkit `cd8bad6`** ("Absorb CI first-frame
init spikes in the trace tail gates"). Date: 2026-09-15.

**FINAL STATUS — all items resolved upstream (2026-10-02).** NK-01…NK-05 were
applied to `main` as commits `2455d7d` (wheel value120), `c7d8777` (modal
keyboard), `2a6a204` (context menu mapping), `e2b17db` (row suffix cap), and
`30df350` (trailing status-bar detail slot, resolving NK-05 with a cleaner
`set_trailing`/`trailing()` API). NK-06 was fixed by `fc40a37` + `b4ad00d`;
NK-07's fullscreen API ships in `window.h`. The vendored copy in
`subprojects/nodalkit` tracks `main` at `ec075a5`; MapperBus builds and tests
green against it (64 OK / 0 fail), uses the handoff status-bar composition
through a `set_trailing` capability probe, F11 through the fullscreen API, and
session hotkeys through the shortcut registry.

**Still local (not yet upstream):** NK-09 (shortcut registry + `Window::
shortcuts()` dispatch), NK-10 (`wl_seat` version 9), and the scroll/dialog
layout regression tests — shipped as
`nodalkit_changes_ec075a5.patch` (base `ec075a5`, 9 files, +287). Apply and
commit on the nodalkit side when ready. NK-11 was retracted (see below).

**NK-12 (local, 2026-10-07).** Wayland file drag-and-drop was a stub:
`wl_data_device` enter/motion/drop did nothing, so a ROM dropped on the window
never arrived. `wayland_input.cpp` now accepts `text/uri-list`, reads the URI
list, and dispatches a `DragDropEvent`. Mime types are stored per offer so a
drag no longer clears the committed clipboard types. This is not in the
`ec075a5` patch; land it with NK-09 and NK-10.

Items NK-01…NK-04 are **implemented and verified** — the exact diff is in
`nodalkit_ui_fixes_cd8bad6.patch` (next to this file) and reproduced at the
bottom of this document. Items NK-05…NK-07 are open requests for triage.

## How to apply

Option A — the working tree of `subprojects/nodalkit` currently carries all
four changes (uncommitted): `git -C subprojects/nodalkit diff` and commit by
pieces, or wholesale.

Option B — apply the patch on any clean `cd8bad6` checkout:

```
git -C subprojects/nodalkit apply nodalkit_ui_fixes_cd8bad6.patch
```

If the working tree was reset in the meantime, re-export the patch from this
repository's copy or re-apply from the diff below.

Suggested commit split (single-line messages, repo style):

1. `Scroll on discrete wheel clicks from value120 events` — NK-01
2. `Give modal overlays the keyboard` — NK-02
3. `Map context menus only while open` — NK-03
4. `Cap preference row suffix width` — NK-04

---

## NK-01 · Wheel clicks do not scroll (P0, fix, implemented)

**Problem.** Mutter delivers physical wheel clicks as
`wl_pointer.axis_value120` (±120 per detent) alongside a **zero** smooth
`wl_pointer.axis` value. NodalKit implemented only the smooth handler
(`WaylandInput::pointer_axis`) and left `pointer_axis_value120` as a stub, so
every wheel click produced `MouseEvent::Type::Scroll` with zero delta — wheels
did nothing anywhere; only touchpad (smooth) scrolling worked. In MapperBus this
made the Settings page content below the fold (the input bindings table, the
lower audio rows) unreachable by mouse.

**Change.** Implement `WaylandInput::pointer_axis_value120` (static member, declared
in `wayland_input.h`, wired in `wl_pointer_listener`): translate each 120 step
into one discrete `MouseEvent::Type::Scroll` (`scroll_dy = -clicks`,
`precise_scrolling = false` → `ScrollArea` moves 40 px per click), dispatch via
the pointer-focus surface owner.

**Files.** `src/platform/linux/wayland_input.{h,cpp}`.

**Verify.** Wheel over any `ScrollArea` (mapperbus Settings, showcase pages):
content scrolls ~40 px per detent; touchpad two-finger scrolling unchanged.

## NK-02 · Modal overlays must own the keyboard (P0, fix, implemented)

**Problem.** Two parts:

1. `Window::show_overlay(modal=true)` saved and restored the previous focus but
   never moved focus into the sheet — Tab, Return, and Escape kept operating on
   the occluded surface (in MapperBus: the game canvas) while a dialog was up.
2. `Window::dispatch_key_event` latched `key_state` (the polled path that
   emulator input backends read) unconditionally, so the game kept reacting to
   keys while a "modal" dialog covered it.

**Change.** In `show_overlay`, after registering a modal overlay, focus its
first focusable descendant (fallback: the overlay root); the existing
`previous_focus` mechanism restores focus on dismiss. In `dispatch_key_event`,
when a visible modal overlay exists, `key_state.fill(false)` instead of
latching.

**Files.** `src/platform/window.cpp` (`dispatch_key_event`, `show_overlay`).

**Verify.** Open a modal dialog: the focus ring appears on a control inside it,
Tab stays inside, keys do not reach `key_state` consumers while open, and focus
returns to the pre-dialog widget on dismiss.

## NK-03 · Context menu should exist in the tree only while open (P0, fix + small API, implemented)

**Problem.** A closed `ContextMenu` stayed mapped (only its paint checked
`open`), so it was hit-testable and focusable while invisible. Consequences:
Tab traversal stopped on the invisible menu; focus stayed on it after Escape;
apps had no signal to reclaim focus.

**Change.** Map in `show_at()` (`set_visible(true)`), unmap in `dismiss()`
(`set_visible(false)` — unmapping also releases focus through
`handle_widget_state_change`), start unmapped in the constructor, and add
`Signal<>& on_dismissed()` — emitted on Escape/outside-click dismissal but **not**
on item activation (activation uses the new private `dismiss_without_activation()`).

**API addition.** `ContextMenu::on_dismissed()` and private
`dismiss_without_activation()` in `include/nk/widgets/context_menu.h`.

**Files.** `include/nk/widgets/context_menu.h`, `src/widgets/context_menu.cpp`.

**Verify.** Closed menu is skipped by Tab and hit tests; Escape dismisses,
releases focus, and fires `on_dismissed`; activating an item fires only
`on_item_activated`.

**Consumer note (MapperBus).** `header_menu_->on_dismissed()` is connected to
refocus the game surface.

## NK-04 · Preference rows starve their titles (P1, fix, implemented)

**Problem.** `PreferencesRow::allocate` gave suffix controls their full natural
width. Wide suffixes (4-way segmented controls, long combo labels) collapsed
row titles into ellipses ("Dead…", "Chann…", "UI De…") and pushed label text
past the row edge.

**Change.** Cap the suffix at 60 % of the row width (still bounded by natural
width and the row minus padding). Segmented controls divide width evenly and
compress gracefully; short combos are unaffected.

**Files.** `src/widgets/preferences.cpp` (`PreferencesRow::allocate`).

**Verify.** A row with "Low/Standard/High/Firm" keeps its full title with a
~299 px control in a ~499 px row; "Preferred device" combos are unchanged.

---

## NK-05 · RESOLVED upstream — trailing status-bar detail

Resolved by `30df350` ("Add a trailing detail slot to the status bar") with a
cleaner API than the removed one: `StatusBar::set_trailing(std::string)` /
`trailing()`, drawn right-aligned in the secondary text color — the handoff
composition restored. MapperBus re-adopted it behind a
`if constexpr (requires { status_bar_->set_trailing(...); })` capability probe,
matching the frontend's existing tolerance pattern.

## NK-06 · RESOLVED upstream — harden the debug bundle renderer

Observed once during UI testing: SIGSEGV at `Widget::is_visible` inside
`save_debug_bundle`'s offscreen re-render while the app was live (stale child
pointer in the tree walk — likely racing the running frame/layout).

Resolved on `main` by two commits: `fc40a37` ("Capture debug screenshots from
the live Vulkan frame instead of a software re-render" — the crashing software
re-render path is gone; `save_debug_screenshot_ppm_file` now reads the live
frame via `capture_debug_screenshot()`) and `b4ad00d` ("Purge destroyed widgets
from the window dirty list"), which closes the stale-pointer class from the
other side. No action needed.

## NK-07 · RESOLVED upstream — fullscreen window API

`nk::Window::set_fullscreen(bool)` / `is_fullscreen()` exist on `main`
(`include/nk/platform/window.h:149`). MapperBus can now implement the F11
fullscreen gap on the frontend side; nothing further needed from the toolkit.

---

## NK-09 · Shortcut registry wiring (implemented, patch attached) — closes the nk/actions gap

`nk::Action`, `nk::ActionGroup`, and `ShortcutBinding` existed, but nothing
resolved key presses against bindings — accelerators had to be hand-rolled per
app (as MapperBus's F11 key check did).

**Implemented in `nodalkit_shortcut_registry_ec075a5.patch`** (base `ec075a5`,
7 files, +221): new `nk::ShortcutRegistry`
(`include/nk/actions/shortcut_registry.h`, `src/actions/shortcut_registry.cpp`)
— `add_action(name, fn)`, `add_shortcut(ShortcutBinding)` (later bindings for
the same shortcut win), `remove_shortcuts_for_action`, `try_activate(Shortcut)`
(a matched-but-disabled action consumes without activating), `action(name)`,
and `shortcuts()` for menu accelerator rendering. `Window::shortcuts()` exposes
a lazy per-window registry; `Window::dispatch_key_event` consults it as a
fallback when the focused chain declines a press — never on auto-repeat and
never while a modal overlay is visible (text fields consume their keys first,
so typing cannot trigger letter bindings). Four new Catch2 cases in
`tests/shortcut_registry_test.cpp`.

**Consumer pattern (MapperBus, committed):** F11 registers
`win.toggle-fullscreen` behind
`if constexpr (requires { window_.shortcuts(); })`, keeping the key-controller
fallback for older revisions — verified live both directions.

**Open follow-ups:** menu accelerator columns rendered from
`shortcuts()` (ContextMenu/MenuBar label work); app-level (cross-window)
registries; allowing modifier-carrying accelerators to fire while a text field
is focused.

## NK-10 · RESOLVED in local tree — `wl_seat` bound below the version needed for wheel clicks

`wayland_backend.cpp` capped `wl_seat` at version 5, so `wl_pointer` negotiated
below 8 and the compositor never sent `axis_value120` — wheel clicks were
invisible to the toolkit regardless of the NK-01 handler. The local tree now
binds the seat at `std::min(version, 9u)` (included in
`nodalkit_changes_ec075a5.patch`). Verified live: value120 events arrive
(`dy=1` per click) and wheel scrolling works end to end.

## NK-11 · RETRACTED — overlay scroll layout works correctly

Earlier observations ("wheel scrolling updated `v_offset` but the screen never
changed") were test-harness artifacts: the emulator window's origin moves every
launch, and several wheel probes landed outside the scroll viewport. Two
deterministic Catch2 cases added in `basic_app_test.cpp` ("Scrolling inside a
presented dialog re-lays-out the overlay", default and Sheet presentation)
prove `scroll_to` inside a modal dialog re-lays-out and repaints correctly on
`ec075a5`. Live-verified on MapperBus with the workaround removed: the full
bindings table scrolls into view with clean striped rows.

The interim MapperBus workaround (`on_scroll_changed` → dialog relayout) was
removed again.

## NK-08 · New finding (post-fix regression pass, 2026-10-02) — preference pages get content-dependent widths (P3)

`Clamp` sizes its child to `min(max_width, natural_width)`, and
`PreferencesGroup` rows keep the group's natural width. Pages without wide
content (MapperBus Audio: short combos only) therefore render ~389 px wide
while others (Input, with the bindings table) render ~499 px — the dialog
content width jumps when switching tabs, and with the NK-04 suffix cap, titles
like "Sample Rate" ellipsize on the narrow page.

Proposal: give `PreferencesGroup` (or the rows) `Expanding` horizontal sizing
inside a clamp, or let the clamp child's minimum track the clamp width, so all
pages of one dialog share one width. Consumer-side workaround: none clean.
Verified live on MapperBus Audio/Input tabs; cosmetic.

---

## Verification (as applied)

- `meson compile -C buildDir mapperbus-gui` — 0 errors (nodalkit builds as part
  of the MapperBus build).
- `just test` — 59 OK, 2 expected fail, 0 fail (includes nodalkit suites).
- `just format-check` — clean; `clang-format` applied to all touched files.
- Live checks on GNOME 50 Wayland: wheel scrolls Settings and reaches the
  bindings table; focus ring lands inside the settings sheet on open; "Deadzone"
  renders untruncated; closed menu absent from Tab order; menu hover/Escape
  behave.

## The diff (nodalkit_ui_fixes_cd8bad6.patch)

```diff
diff --git a/include/nk/widgets/context_menu.h b/include/nk/widgets/context_menu.h
index 648f421..ad79a72 100644
--- a/include/nk/widgets/context_menu.h
+++ b/include/nk/widgets/context_menu.h
@@ -40,6 +40,10 @@ public:
     /// Emitted when an item is activated, with its index.
     Signal<int>& on_item_activated();
 
+    /// Emitted when the menu is dismissed without an item activation
+    /// (Escape or outside click). The menu is already hidden when this fires.
+    Signal<>& on_dismissed();
+
     // --- Widget overrides ---
     [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;
     bool handle_mouse_event(const MouseEvent& event) override;
@@ -54,6 +58,9 @@ protected:
 private:
     struct Impl;
     std::unique_ptr<Impl> impl_;
+
+    /// Close the menu without emitting on_dismissed; used on activation.
+    void dismiss_without_activation();
 };
 
 } // namespace nk
diff --git a/src/platform/linux/wayland_input.cpp b/src/platform/linux/wayland_input.cpp
index 52178c2..9a5cb57 100644
--- a/src/platform/linux/wayland_input.cpp
+++ b/src/platform/linux/wayland_input.cpp
@@ -322,10 +322,30 @@ static void pointer_axis_discrete(void* /*data*/,
                                   uint32_t /*axis*/,
                                   int32_t /*discrete*/) {}
 
-static void pointer_axis_value120(void* /*data*/,
-                                  wl_pointer* /*pointer*/,
-                                  uint32_t /*axis*/,
-                                  int32_t /*value120*/) {}
+void WaylandInput::pointer_axis_value120(void* data,
+                                         wl_pointer* /*pointer*/,
+                                         uint32_t axis,
+                                         int32_t value120) {
+    // Wheel clicks arrive as value120 steps, often alongside a zero smooth
+    // axis value. Translate each 120 step into one discrete scroll click.
+    auto* self = static_cast<WaylandInput*>(data);
+    if (self == nullptr || self->pointer_focus_ == nullptr || value120 == 0) {
+        return;
+    }
+
+    MouseEvent me{};
+    me.type = MouseEvent::Type::Scroll;
+    me.x = self->pointer_x_;
+    me.y = self->pointer_y_;
+    const auto clicks = static_cast<float>(value120) / 120.0F;
+    if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) {
+        me.scroll_dy = -clicks; // Wayland positive = scroll down.
+    } else {
+        me.scroll_dx = clicks;
+    }
+    me.precise_scrolling = false;
+    self->pointer_focus_->owner().dispatch_mouse_event(me);
+}
 
 static void pointer_axis_relative_direction(void* /*data*/,
                                             wl_pointer* /*pointer*/,
@@ -357,7 +377,7 @@ static constexpr struct wl_pointer_listener pointer_listener = {
     .axis_source = pointer_axis_source,
     .axis_stop = pointer_axis_stop,
     .axis_discrete = pointer_axis_discrete,
-    .axis_value120 = pointer_axis_value120,
+    .axis_value120 = WaylandInput::pointer_axis_value120,
     .axis_relative_direction = pointer_axis_relative_direction,
 #ifdef WL_POINTER_WARP_SINCE_VERSION
     .warp = WaylandInput::pointer_warp,
diff --git a/src/platform/linux/wayland_input.h b/src/platform/linux/wayland_input.h
index 63450c2..9de4b58 100644
--- a/src/platform/linux/wayland_input.h
+++ b/src/platform/linux/wayland_input.h
@@ -84,6 +84,8 @@ public:
                                uint32_t state);
     static void
     pointer_axis(void* data, wl_pointer* pointer, uint32_t time, uint32_t axis, wl_fixed_t value);
+    static void
+    pointer_axis_value120(void* data, wl_pointer* pointer, uint32_t axis, int32_t value120);
     // Wayland >= 1.26 (WL_POINTER_WARP_SINCE_VERSION).
     static void
     pointer_warp(void* data, wl_pointer* pointer, wl_fixed_t surface_x, wl_fixed_t surface_y);
diff --git a/src/platform/window.cpp b/src/platform/window.cpp
index 3f4534f..85bdcce 100644
--- a/src/platform/window.cpp
+++ b/src/platform/window.cpp
@@ -3626,9 +3626,19 @@ void Window::dispatch_key_event(const KeyEvent& event) {
     if (event.type == KeyEvent::Type::Press) {
         impl_->last_input_keyboard = true;
     }
-    const auto key_index = static_cast<std::size_t>(event.key);
-    if (key_index < impl_->key_state.size()) {
-        impl_->key_state[key_index] = event.type == KeyEvent::Type::Press;
+    // A visible modal sheet owns input: game-style polled keys (key_state)
+    // must not latch while it is up, or the surface behind keeps reacting.
+    const bool modal_overlay_visible = std::any_of(
+        impl_->overlays.begin(), impl_->overlays.end(), [](const Impl::OverlayEntry& entry) {
+            return entry.modal && entry.widget != nullptr && entry.widget->is_visible();
+        });
+    if (modal_overlay_visible) {
+        impl_->key_state.fill(false);
+    } else {
+        const auto key_index = static_cast<std::size_t>(event.key);
+        if (key_index < impl_->key_state.size()) {
+            impl_->key_state[key_index] = event.type == KeyEvent::Type::Press;
+        }
     }
 
     std::vector<std::shared_ptr<Widget>> overlay_widgets;
@@ -4050,11 +4060,19 @@ void Window::show_overlay(std::shared_ptr<Widget> overlay, bool modal) {
 
     overlay->set_host_window(this);
     append_unique_widget(impl_->dirty_widgets, overlay.get());
+    Widget* overlay_ptr = overlay.get();
     impl_->overlays.push_back({std::move(overlay),
                                modal,
                                modal && impl_->focused_widget != nullptr
                                    ? impl_->focused_widget->shared_from_this()
                                    : std::weak_ptr<Widget>{}});
+    if (modal) {
+        // Modal sheets own the keyboard: move focus inside so Tab, Return,
+        // and Escape operate on the sheet instead of the occluded surface.
+        std::vector<Widget*> focusable;
+        collect_focusable_widgets(overlay_ptr, focusable);
+        focus_widget(focusable.empty() ? overlay_ptr : focusable.front());
+    }
     impl_->needs_layout = true;
     request_frame(FrameRequestReason::OverlayChanged);
 }
diff --git a/src/widgets/context_menu.cpp b/src/widgets/context_menu.cpp
index 0ddff91..a8a4ff1 100644
--- a/src/widgets/context_menu.cpp
+++ b/src/widgets/context_menu.cpp
@@ -79,6 +79,7 @@ int item_index_at(const PopupGeometry& geometry,
 struct ContextMenu::Impl {
     std::vector<MenuItemEntry> items;
     Signal<int> item_activated;
+    Signal<> dismissed;
     bool open = false;
     Point position;
     int hovered_index = -1;
@@ -91,6 +92,7 @@ std::shared_ptr<ContextMenu> ContextMenu::create() {
 
 ContextMenu::ContextMenu() : impl_(std::make_unique<Impl>()) {
     set_focusable(true);
+    set_visible(false);
     add_style_class("context-menu");
     auto& accessible = ensure_accessible();
     accessible.set_role(AccessibleRole::Menu);
@@ -123,6 +125,7 @@ void ContextMenu::show_at(Point position) {
     impl_->open = true;
     impl_->hovered_index = -1;
     impl_->armed_index = -1;
+    set_visible(true);
     queue_redraw();
 }
 
@@ -134,7 +137,27 @@ void ContextMenu::dismiss() {
     impl_->open = false;
     impl_->hovered_index = -1;
     impl_->armed_index = -1;
+    // Unmap so hit tests and the focus chain skip the closed menu. Unmapping
+    // also releases focus if the menu held it.
+    set_visible(false);
     queue_redraw();
+    impl_->dismissed.emit();
+}
+
+void ContextMenu::dismiss_without_activation() {
+    if (!impl_->open) {
+        return;
+    }
+    preserve_damage_regions_for_next_redraw();
+    impl_->open = false;
+    impl_->hovered_index = -1;
+    impl_->armed_index = -1;
+    set_visible(false);
+    queue_redraw();
+}
+
+Signal<>& ContextMenu::on_dismissed() {
+    return impl_->dismissed;
 }
 
 bool ContextMenu::is_open() const {
@@ -217,7 +240,7 @@ bool ContextMenu::handle_mouse_event(const MouseEvent& event) {
         const int activated_index = impl_->armed_index;
         impl_->armed_index = -1;
         if (activated) {
-            dismiss();
+            dismiss_without_activation();
             impl_->item_activated.emit(activated_index);
         }
         return true;
@@ -287,7 +310,7 @@ bool ContextMenu::handle_key_event(const KeyEvent& event) {
     case KeyCode::Space:
         if (impl_->hovered_index >= 0) {
             const int activated_index = impl_->hovered_index;
-            dismiss();
+            dismiss_without_activation();
             impl_->item_activated.emit(activated_index);
         }
         return true;
diff --git a/src/widgets/preferences.cpp b/src/widgets/preferences.cpp
index 2d6cf5e..c2c494a 100644
--- a/src/widgets/preferences.cpp
+++ b/src/widgets/preferences.cpp
@@ -139,7 +139,11 @@ void PreferencesRow::allocate(const Rect& allocation) {
     }
     const float padding = theme_number("padding-x", 16.0F);
     const auto request = impl_->suffix->measure({0.0F, 0.0F, allocation.width, allocation.height});
-    const float width = std::min(request.natural_width, std::max(0.0F, allocation.width - padding));
+    // Cap wide suffixes (segmented controls, long combos) so the title keeps
+    // a readable share instead of collapsing into an ellipsis.
+    const float max_suffix_width = std::max(0.0F, allocation.width * 0.6F);
+    const float width = std::min(std::min(request.natural_width, max_suffix_width),
+                                 std::max(0.0F, allocation.width - padding));
     const float height = std::min(request.natural_height, allocation.height);
     impl_->suffix->allocate({allocation.right() - padding - width,
                              allocation.y + ((allocation.height - height) * 0.5F),
```
