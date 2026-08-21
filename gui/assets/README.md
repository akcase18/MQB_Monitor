# gui/assets/

Font source files (as LVGL C font descriptors) and their licenses.

## Files

- **`fonts/`** — LVGL font data, converted from source fonts and compiled directly into the firmware:
  - `jetbrains_mono_11/14/16/24.c`, `jetbrains_mono_bold_24/30/46/56/96.c` — JetBrains Mono, the app's one typeface, at every size a `Typography` role in [gui/theme](../theme/README.md) needs. Regular weight is used for labels/UI text, Bold for numeric readouts and nav bar titles. Sizes were chosen from actual panel-cell dimensions (e.g. the smallest, a 4-character value like "2546", has to fit the tightest cell — the 6-panel grid's ~150px-wide cells — without overflowing).
  - `icon_day_night_28.c` — just the sun/moon glyphs (U+F185/U+F186) from Font Awesome Free Solid, used by the Settings screen's Day/Night toggle. A separate font from LVGL's built-in symbol set, which has nothing celestial in its curated icon subset.
- **`font-licenses/`** — `JetBrainsMono-LICENSE.txt` and `FontAwesome-LICENSE.txt`, both SIL Open Font License 1.1.

## Adding a new font size or icon

Fonts are converted via [LVGL's online font converter](https://lvgl.io/tools/fontconverter) (or the equivalent CLI) into a `.c` file in `fonts/`, declared `extern const lv_font_t <name>` in [gui/theme/src/theme.cpp](../theme/src/theme.cpp), and assigned to the relevant `Typography` field in `THEME_DEFAULT`. Remember to add `+<../gui/assets/fonts>` coverage in `platformio.ini`'s `build_src_filter` for any new environment (both `teensymm` and `simulator` already include the whole directory).
