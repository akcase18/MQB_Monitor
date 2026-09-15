# gui/theme/

Palette, typography, and metrics tokens for every screen and widget — the one place a full reskin touches. Nothing in [gui/widgets](../widgets/README.md) or [gui/screens](../screens/README.md) references `THEME_DEFAULT` by name; they only ever see a `const Theme &` passed in at `init()`.

## Files

- **`inc/theme.hpp`** — the token structs:
  - `Palette` — `bg`, `fg`, `accent` (pressed-button highlight, also the user-selectable accent color), `warn_lo`/`warn_hi` (warning highlight colors), `panel_border`, `range_overflow` (Bar/Gauge fill color when a value is outside its `SignalSpec::display_min/max` — distinct from a configured warning, since a value can peg a graphic at 100%/0% without ever crossing a warning threshold), `rec_indicator` (InfoScreen's blinking capture LED — its own token rather than reusing `warn_hi`: both are red by convention, but a warning means "something is wrong" and this means "recording").
  - `Typography` — a font per role: `data_sm`/`data_lg`/`data_hero`/`data_mega` (readout sizes, from small-panel to true hero slots), `label_sm`/`label_lg` (panel name labels), `unit` (value's unit tag, its own smaller size regardless of panel tier), `ui` (general screen/button text), `symbol_lg` (an LVGL built-in font, for done/settings button glyphs — `LV_SYMBOL_*` glyphs aren't in the custom JetBrains Mono fonts), `title` (nav bar titles), `icon_day_night` (sun/moon glyphs).
  - `Metrics` — screen resolution, panel corner radius/border width/gap/margin, and the legacy small/large panel height fractions (`panel_height_sm_pct`/`panel_height_lg_pct`, used only by the 5-panel layout — see [gui/screens](../screens/README.md)).
  - `Theme { Palette, Typography, Metrics }` — the full bundle passed to every screen's `init()`.
  - `THEME_DEFAULT` — black background, white text, green accent, blue/red low/high warnings.
  - `ACCENT_PRESETS[ACCENT_PRESET_COUNT]` — 9 packed `0xRRGGBB` accent color choices shown in the Theme Picker screen, stored as plain `uint32_t` (matching LVGL's built-in palette mains) rather than `lv_color_t` so the same values are usable from `core::Settings::accent_color`, which has no LVGL dependency.
  - `ICON_SUN`/`ICON_MOON` — UTF-8 string literals for the Day/Night toggle's glyphs (Font Awesome Free Solid's fa-sun/fa-moon), the same pattern LVGL's own `LV_SYMBOL_*` macros use.

- **`src/theme.cpp`** — `THEME_DEFAULT`/`ACCENT_PRESETS`' actual values, plus `extern "C"` declarations pulling in the font data from [gui/assets/fonts](../assets/README.md).

## Adding a new theme variant

Add another `const Theme` alongside `THEME_DEFAULT` and pass it wherever `THEME_DEFAULT` is currently passed in a composition root — nothing in `gui/widgets` or `gui/screens` needs to change.

## `THEME_GAUGE` — the 320×172 strip

A second `const Theme` remapping the same nine JetBrains faces, plus metrics to match. No new font generation.

**The data faces were picked by arithmetic, and the build plan's original recommendation did not survive it.** A 3-up `LayoutStyle::Strip` column on this panel is `320 − 2·screen_margin(3) − 2·panel_gap(3) = 308`, `÷3 = 102 px`, less `2·panel_border_width(2)` = **98 px usable**. The plan sized the readout at `bold_46` assuming a three-character worst case — but `unit_decimals()` gives Pressure two decimals in metric and one in imperial, so oil pressure reads `10.34` bar or `150.0` psi: **five characters**. `bold_46` advances 27.62 px/char and needs 138 px, which clips. `bold_30` advances 18.00 and needs 90, which fits.

The zoom view is the opposite and the plan had it right: one panel across 310 px, where `bold_96` needs 288 for five characters. That is the one place the largest face earns its keep, and why it was retained.

`panel_height_sm_pct` / `panel_height_lg_pct` are unused under `Strip` (every panel is full height) and are set to an even split rather than carried over, so nothing there reads as a meaningful ratio.

**`unit_stacked` is set here, and the arithmetic above is why.** The 98 px budget covers the *value*; the panel also draws a unit tag, and normally places it to the right of a value that is itself centred — so the pair overhangs the centre line by half the value plus the whole tag. "77.2 psi" therefore rendered as "77.2 ps", with the tag running off the panel edge, on a build whose font-fit test was entirely green. Stacking the tag on its own line makes the constraint `max(value, tag)` rather than their sum. The cost moves to the vertical budget, where a 172 px panel has room for three lines and `test_gui_theme` now counts all three.

All of this is pinned by [`test/test_gui_theme/`](../../test/README.md) against the real font data — a value that clips at full scale is not something a code review catches, nor reliably a glance at the simulator.
