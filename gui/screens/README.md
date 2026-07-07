# gui/screens/

Every screen in the app, the panel-layout algorithm, and small helpers shared across screens. Like [gui/widgets](../widgets/README.md), none of this includes `core/` — screens are driven by plain-data view-models and typed callbacks, wired to real vehicle data only by [gui/bindings](../bindings/README.md) and `src/main.cpp`.

## Navigation

```
InfoScreen  ──(swipe down gesture)──>  SettingsScreen ──(Theme row)──> ThemePickerScreen
     │
     └─(tap a panel)──> ConfiguratorScreen ──(Signal row)────> SignalPickerScreen
                              │
                              └─(Warning Thresholds row)──> WarningConfigScreen
```

All screens except `InfoScreen` use a top nav bar (title + X close button) over stacked, divider-separated sections; `InfoScreen` itself has no chrome — it's the panel grid, full-screen. Navigation callbacks live in `src/main.cpp`, not in the screens themselves — every `DoneCallback`/`PickXCallback` is a plain function pointer the composition root wires up.

## Files

- **`inc/layout.hpp`** / **`src/layout.cpp`** — `compute_layout(count, orientation, out[])`: pure grid-slot placement for 1–6 panels, no LVGL, no `core::`. Each panel count gets its own shape (horizontal orientation):
  - **1** — one full-screen panel.
  - **2** — two equal full-height columns.
  - **3** — a wide hero panel plus two stacked minor panels (not three narrow columns) — the hero/stack width split (`HERO_COLSPAN`/`STACK_COLSPAN`) is a starting ratio pending an on-hardware check.
  - **4** — an even 2×2, all panels equal.
  - **5** — 3 small panels over 2 large ones (the original, legacy fixed layout — the only count that still uses `Theme::Metrics::panel_height_sm_pct/lg_pct` rather than a 50/50 row split).
  - **6** — an even 2×3, all panels equal (small tier).
  
  Vertical orientation transposes whichever shape the count produces (small band ↔ left/right instead of top/bottom) rather than getting its own bespoke shapes. Cheap to unit test — see [test/test_gui_layout](../../test/README.md).

- **`inc/info_screen.hpp`** / **`src/info_screen.cpp`** — `InfoScreen`, the main dashboard. Places up to `MAX_PANELS` `PanelWidget`s per `compute_layout()`, pulls each one's view-model through a caller-supplied `ViewModelBuilder` callback (so this screen never touches `core::`/`gui/bindings` directly), and fires a `PanelPressCallback` on tap. `rebuild(count, orientation)` re-lays-out and shows/hides panels — call it whenever `Settings::num_panels`/`orientation` change. `refresh()` re-pulls every active panel's view-model, called periodically to keep values live.

- **`inc/configurator_screen.hpp`** / **`src/configurator_screen.cpp`** — `ConfiguratorScreen`, opened by tapping a panel. Sections: Signal (tappable row → `SignalPickerScreen`), Units (Imperial/Metric 2-segment matrix), Display (Digital/Bar/Gauge 3-segment matrix), Warning Thresholds (tappable row → `WarningConfigScreen`). Signal and Warning Thresholds hand off to dedicated screens rather than embedding pickers inline — an embedded roller here originally came out taller than expected and spilled into the section below it.

- **`inc/signal_picker_screen.hpp`** / **`src/signal_picker_screen.cpp`** — `SignalPickerScreen`, a full-screen `lv_roller` over every catalog signal. Exists as its own screen (rather than living inline on `ConfiguratorScreen`) because `lv_roller_set_visible_row_count()`'s auto-sizing reserves extra height beyond N literal rows for the partial-row fade effect, which didn't fit alongside the rest of the Configurator's layout.

- **`inc/warning_config_screen.hpp`** / **`src/warning_config_screen.cpp`** — `WarningConfigScreen`, the low/high threshold editor for one signal — toggle each bound on/off, adjust with +/- buttons (tap-to-step, hold-to-repeat) bounded by that signal's `SignalSpec::display_min/max`. Actually wired to persistence (via `DoneCallback` → [gui/bindings](../bindings/README.md)'s `apply_warning_change`) — the original version of this screen had a dead Done button that never saved anything.

- **`inc/settings_screen.hpp`** / **`src/settings_screen.cpp`** — `SettingsScreen`: a Brightness section (one slider, 5%-stepped, editing whichever of day/night the adjacent Day/Night toggle button currently selects — day and night are rarely tuned side by side, so one slider covers both rather than showing two), a Panels section (1–6 count picker), and a compact Theme row that hands off to `ThemePickerScreen`. The Day/Night toggle is a single button whose sun/moon glyph swaps on tap; toggling and every slider move fire `PreviewBrightnessCallback` immediately so a change is visible on the real backlight without waiting for Done — useful mid-drive, not just a setting tuned once and forgotten.

- **`inc/theme_picker_screen.hpp`** / **`src/theme_picker_screen.cpp`** — `ThemePickerScreen`: a grid of `gui::ACCENT_PRESETS` swatches. Tapping one fires `DoneCallback` immediately (no separate confirmation step) — there's nothing to review before committing to a color choice.

- **`inc/screen_helpers.hpp`** / **`src/screen_helpers.cpp`** — shared pieces every screen uses: `create_screen()` (a full-size, theme-backgrounded top-level screen object), `create_close_button()` (the top-right X every screen but `InfoScreen` uses to exit, freeing the rest of the screen for a nav bar over stacked sections — currently used by all five of Configurator/SignalPicker/WarningConfig/Settings/ThemePicker), `create_divider()` (a full-width horizontal rule separating stacked sections). `create_done_button()` (a bottom-right checkmark, the original exit style before the nav-bar pattern) is also declared here but currently unused by any screen.

## Adding a new screen

Follow an existing screen's shape: a `class` owning its own `lv_obj_t *screen_`, an `init()` that builds it once against a `Theme` and a set of typed callbacks, an `open()` if it needs per-open state, and `static` LVGL event handlers that dispatch to instance methods via `lv_event_get_user_data()`. Wire it into `src/main.cpp`'s `Composition` struct, `build_screens()`/`destroy_screens()`, and whatever triggers navigation to it.
