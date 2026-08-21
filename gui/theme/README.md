# gui/theme/

Palette, typography, and metrics tokens for every screen and widget — the one place a full reskin touches. Nothing in [gui/widgets](../widgets/README.md) or [gui/screens](../screens/README.md) references `THEME_DEFAULT` by name; they only ever see a `const Theme &` passed in at `init()`.

## Files

- **`inc/theme.hpp`** — the token structs:
  - `Palette` — `bg`, `fg`, `accent` (pressed-button highlight, also the user-selectable accent color), `warn_lo`/`warn_hi` (warning highlight colors), `panel_border`, `range_overflow` (Bar/Gauge fill color when a value is outside its `SignalSpec::display_min/max` — distinct from a configured warning, since a value can peg a graphic at 100%/0% without ever crossing a warning threshold).
  - `Typography` — a font per role: `data_sm`/`data_lg`/`data_hero`/`data_mega` (readout sizes, from small-panel to true hero slots), `label_sm`/`label_lg` (panel name labels), `unit` (value's unit tag, its own smaller size regardless of panel tier), `ui` (general screen/button text), `symbol_lg` (an LVGL built-in font, for done/settings button glyphs — `LV_SYMBOL_*` glyphs aren't in the custom JetBrains Mono fonts), `title` (nav bar titles), `icon_day_night` (sun/moon glyphs), `icon_status` (a smaller LVGL built-in symbol font — `symbol_lg`'s 32px reads as too large sitting over InfoScreen's dense panel grid — for small corner status glyphs like `InfoScreen::set_logging_active()`'s indicator).
  - `Metrics` — screen resolution, panel corner radius/border width/gap/margin, and the legacy small/large panel height fractions (`panel_height_sm_pct`/`panel_height_lg_pct`, used only by the 5-panel layout — see [gui/screens](../screens/README.md)).
  - `Theme { Palette, Typography, Metrics }` — the full bundle passed to every screen's `init()`.
  - `THEME_DEFAULT` — black background, white text, green accent, blue/red low/high warnings.
  - `ACCENT_PRESETS[ACCENT_PRESET_COUNT]` — 9 packed `0xRRGGBB` accent color choices shown in the Theme Picker screen, stored as plain `uint32_t` (matching LVGL's built-in palette mains) rather than `lv_color_t` so the same values are usable from `core::Settings::accent_color`, which has no LVGL dependency.
  - `ICON_SUN`/`ICON_MOON` — UTF-8 string literals for the Day/Night toggle's glyphs (Font Awesome Free Solid's fa-sun/fa-moon), the same pattern LVGL's own `LV_SYMBOL_*` macros use.

- **`src/theme.cpp`** — `THEME_DEFAULT`/`ACCENT_PRESETS`' actual values, plus `extern "C"` declarations pulling in the font data from [gui/assets/fonts](../assets/README.md).

## Adding a new theme variant

Add another `const Theme` alongside `THEME_DEFAULT` and pass it wherever `THEME_DEFAULT` is currently passed in `src/main.cpp`'s `build_screens()` — nothing in `gui/widgets` or `gui/screens` needs to change.
