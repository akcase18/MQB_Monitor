# gui/screens/

Every screen in the app, the panel-layout algorithm, and small helpers shared across screens. Like [gui/widgets](../widgets/README.md), none of this includes `core/` — screens are driven by plain-data view-models and typed callbacks, wired to real vehicle data only by [gui/bindings](../bindings/README.md) and `src/main.cpp`.

## Navigation

```
InfoScreen  ──(swipe down gesture)──>  SettingsScreen ──(Theme row)──────────> ThemePickerScreen
     │                                       │
     │                                       └─(Wireless Setup row)──> WirelessSetupScreen
     │
     └─(tap a panel)──> ConfiguratorScreen ──(Signal row)────> SignalPickerScreen
                              │
                              └─(Warning Thresholds row)──> WarningConfigScreen
```

All screens except `InfoScreen` use a top nav bar (title + X close button) over stacked, divider-separated sections; `InfoScreen` itself has no chrome beyond the panel grid, full-screen, aside from a small top-right status glyph (`set_logging_active()` — see below). Navigation callbacks live in `src/main.cpp`, not in the screens themselves — every `DoneCallback`/`PickXCallback` is a plain function pointer the composition root wires up.

## Files

- **`inc/layout.hpp`** / **`src/layout.cpp`** — `compute_layout(count, orientation, out[])`: pure grid-slot placement for 1–6 panels, no LVGL, no `core::`. Each panel count gets its own shape (horizontal orientation):
  - **1** — one full-screen panel.
  - **2** — two equal full-height columns.
  - **3** — a wide hero panel plus two stacked minor panels (not three narrow columns) — the hero/stack width split (`HERO_COLSPAN`/`STACK_COLSPAN`) is a starting ratio pending an on-hardware check.
  - **4** — an even 2×2, all panels equal.
  - **5** — 3 small panels over 2 large ones (the original, legacy fixed layout — the only count that still uses `Theme::Metrics::panel_height_sm_pct/lg_pct` rather than a 50/50 row split).
  - **6** — an even 2×3, all panels equal (small tier).
  
  Vertical orientation transposes whichever shape the count produces (small band ↔ left/right instead of top/bottom) for every count except 5 — a straight transpose of 5's 3-over-2 stacks 3 skinny panels in one column and 2 in the other, which reads poorly on a narrow portrait screen, so 5 gets its own bespoke Vertical shape instead: a 2×2 of small panels over one wider, higher-priority panel. Cheap to unit test — see [test/test_gui_layout](../../test/README.md).

- **`inc/info_screen.hpp`** / **`src/info_screen.cpp`** — `InfoScreen`, the main dashboard. Places up to `MAX_PANELS` `PanelWidget`s per `compute_layout()`, pulls each one's view-model through a caller-supplied `ViewModelBuilder` callback (so this screen never touches `core::`/`gui/bindings` directly), and fires a `PanelPressCallback` on tap. `rebuild(count, orientation)` re-lays-out and shows/hides panels — call it whenever `Settings::num_panels` changes. Orientation, by contrast, is boot-time-only (see `platform.hpp`'s `DisplayOrientation`): `src/main.cpp` always calls `rebuild()` with the orientation actually active on the physical display, not `core::Settings::orientation`'s possibly-just-staged value, so a Settings change never reflows the grid against a canvas that hasn't physically rotated yet. `refresh()` re-pulls every active panel's view-model, called periodically to keep values live. `set_logging_active(bool)` shows/hides a small, muted `LV_SYMBOL_SD_CARD` glyph in the top-right corner (`LV_OBJ_FLAG_IGNORE_LAYOUT`'d out of the grid it sits over) — `InfoScreen` takes a plain bool rather than depending on `core::Logger` (see [core/logging](../../core/logging/README.md)) directly, same reasoning as `ViewModelBuilder`.

- **`inc/configurator_screen.hpp`** / **`src/configurator_screen.cpp`** — `ConfiguratorScreen`, opened by tapping a panel. Sections: Signal (tappable row → `SignalPickerScreen`), Units (Imperial/Metric 2-segment matrix), Display (Digital/Bar/Gauge 3-segment matrix), Warning Thresholds (tappable row → `WarningConfigScreen`). Signal and Warning Thresholds hand off to dedicated screens rather than embedding pickers inline — an embedded roller here originally came out taller than expected and spilled into the section below it.

- **`inc/signal_picker_screen.hpp`** / **`src/signal_picker_screen.cpp`** — `SignalPickerScreen`, a full-screen `lv_roller` over every catalog signal. Exists as its own screen (rather than living inline on `ConfiguratorScreen`) because `lv_roller_set_visible_row_count()`'s auto-sizing reserves extra height beyond N literal rows for the partial-row fade effect, which didn't fit alongside the rest of the Configurator's layout.

- **`inc/warning_config_screen.hpp`** / **`src/warning_config_screen.cpp`** — `WarningConfigScreen`, the low/high threshold editor for one signal — toggle each bound on/off, adjust with +/- buttons (tap-to-step, hold-to-repeat) bounded by that signal's `SignalSpec::display_min/max`. Actually wired to persistence (via `DoneCallback` → [gui/bindings](../bindings/README.md)'s `apply_warning_change`) — the original version of this screen had a dead Done button that never saved anything.

- **`inc/settings_screen.hpp`** / **`src/settings_screen.cpp`** — `SettingsScreen`: a Brightness section (one slider, 5%-stepped, editing whichever of day/night the adjacent Day/Night toggle button currently selects — day and night are rarely tuned side by side, so one slider covers both rather than showing two), a Panels section (1–6 count picker), and compact rows — Theme (hands off to `ThemePickerScreen`), Orientation (an H/V toggle button), and Wireless Setup (hands off to `WirelessSetupScreen`). The Day/Night toggle is a single button whose sun/moon glyph swaps on tap; toggling and every slider move fire `PreviewBrightnessCallback` immediately so a change is visible on the real backlight without waiting for Done — useful mid-drive, not just a setting tuned once and forgotten. The Orientation toggle doesn't flip immediately on tap — it opens an `lv_msgbox` confirm/cancel popup first, since a physical rotation only takes effect on the next restart (boot-time-only, see `platform.hpp`'s `DisplayOrientation`) and a silent flip with no visible effect would look broken. Portrait gets its own layout throughout this screen (label-above-control rows instead of label-beside-control, taller touch targets using the extra vertical room) rather than a shrunk copy of Landscape's.

- **`inc/theme_picker_screen.hpp`** / **`src/theme_picker_screen.cpp`** — `ThemePickerScreen`: a grid of `gui::ACCENT_PRESETS` swatches. Tapping one fires `DoneCallback` immediately (no separate confirmation step) — there's nothing to review before committing to a color choice.

- **`inc/wireless_setup_screen.hpp`** / **`src/wireless_setup_screen.cpp`** — `WirelessSetupScreen`: brings up the ESP32-C3's Wi-Fi AP so the rest of configuration can happen from a phone browser instead of this touchscreen. Opening the screen activates the AP immediately (no Activate step). Two mutually-exclusive states driven by `WirelessSetupViewModel::phase` (Idle/Activating → spinner; Active → an `lv_qrcode` linking to the config page plus the SSID/URL as text — the AP is deliberately open, no password). `refresh()` re-pulls the phase without changing screens, so the async Activating → Active round trip (and an idle-timeout auto-deactivation) show up live while the screen is open — see [gui/bindings](../bindings/README.md)'s `RemoteBinding`. The close (X) button is the only way out and deactivates the AP on the way, optimistically rather than waiting to watch the state machine settle — there's no separate Turn Off control.

- **`inc/screen_helpers.hpp`** / **`src/screen_helpers.cpp`** — shared pieces every screen uses: `create_screen()` (a full-size, theme-backgrounded top-level screen object), `create_close_button()` (the top-right X every screen but `InfoScreen` uses to exit, freeing the rest of the screen for a nav bar over stacked sections — currently used by all five of Configurator/SignalPicker/WarningConfig/Settings/ThemePicker), `create_divider()` (a full-width horizontal rule separating stacked sections). `create_done_button()` (a bottom-right checkmark, the original exit style before the nav-bar pattern) is also declared here but currently unused by any screen.

## Adding a new screen

Follow an existing screen's shape: a `class` owning its own `lv_obj_t *screen_`, an `init()` that builds it once against a `Theme` and a set of typed callbacks, an `open()` if it needs per-open state, and `static` LVGL event handlers that dispatch to instance methods via `lv_event_get_user_data()`. Wire it into `src/main.cpp`'s `Composition` struct, `build_screens()`/`destroy_screens()`, and whatever triggers navigation to it.
