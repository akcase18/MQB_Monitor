# gui/widgets/

`PanelWidget` — the single reusable info-panel building block used by the Info screen. Styled purely from a `Theme` and driven purely by a plain-data `PanelViewModel`; it has no pointer into `core::vehicle`/`core::config`, so it's previewable with fabricated data independent of CAN/config wiring. [gui/bindings](../bindings/README.md) is the only place allowed to build a real `PanelViewModel` from vehicle data.

## Files

- **`inc/panel_widget.hpp`** / **`src/panel_widget.cpp`**:
  - `DisplayMode { Digital, Bar, Gauge }` — how a panel presents its value. Digital is the bare tabular number; Bar/Gauge add a fast-glance graphic *alongside* it, never instead of it.
  - `PanelViewModel { name, unit_label, value_str, warn_lo_active, warn_hi_active, mode, is_text, range_pct }` — everything one panel needs to render. `is_text` marks a value that's words (an Enum's label): it gets the size a number would in that panel, or the biggest smaller one that fits it on one line and has all its letters (the value fonts carry only the letters active labels use). `range_pct` (0..1) is the value's position within its signal's display range, meaningful only for Bar/Gauge.
  - `PanelWidget::init(parent, theme)` — pre-creates the button plus *all three* display-mode visuals (label, `lv_bar`, `lv_arc`) up front, hidden by default. Changing a panel's mode later (rare — only via the Configurator) just shows/hides existing objects rather than allocating/freeing LVGL objects on the hot refresh path.
  - `set_view_model(vm)` — applies text, active display mode, and warning highlight. A warning repaints the whole panel background `warn_lo`/`warn_hi`; the Bar/Gauge fill deliberately stays accent-colored (tinting it to match made the graphic vanish into its own panel), and is inset inside its track so the track color rings it — the only separation available when the user's accent is itself blue or red.
  - `set_visible(visible)` — shows/hides without destroying, used when the active panel count shrinks.

  Sizing is entirely the parent's responsibility — `PanelWidget` never sets its own width/height. It sizes its *contents* to whatever box it's given, which is what lets the same widget work whether it's previewed standalone or placed by `gui::compute_layout()` (see [gui/screens](../screens/README.md)).

## How a panel's contents are sized

Everything comes from the panel's own content box (its cell, less the 2 px border and 4 px padding), so a bigger cell always gets bigger contents. Nothing depends on which layout or slot the panel is in. The work happens only when the cell, mode, name or (for words) value changes, in `resize_contents()`, which is `FLASHMEM` on the Teensy.

1. **Caption** at the bottom: `label_xl` (24 px) in a panel at least 280×200, `label_lg` (16 px) if either side is at least 200, else `label_sm` (14 px). It steps down a size rather than wrap; only `label_sm` wraps, to two lines. The value and graphic share what's left above it.
2. **Value font**: the biggest of `Typography::data` (30, 38, 54, 64, 80, 92, 128 px) that holds the worst-case number — 5 glyphs plus a 3-glyph unit ("-14.5 psi"), or 4 glyphs plus "lb-ft" — so a panel's numbers never change size as the value moves. A Bar also needs room for its bar underneath; a Gauge's 5 glyphs must fit inside the ring. If none fits, `data_text` (24 px) is the last resort.
3. **Unit tag**: the biggest of `label_xl`/`label_lg`/`label_sm` no taller than a quarter of the value's line, else `unit` (11 px). It drops to a smaller one when that's what lets the value be a size bigger. Its top lines up with the top of the digits. Hidden for Gauge.
4. **Gauge ring**: as big as the room allows, counting only its drawn part (the box under the gap at the bottom may overlap the caption), then shrunk up to 8% to clear the panel's edges, but never so far the value stops fitting. The stroke scales with it (diameter / 14, 7–16 px).
5. **Bar**: 85% of the width; a tenth of the room tall (18–32 px), under the digits.

**Vertical position**: every mode centers its digits on the same line: where the center of the biggest ring the room holds would be, with that ring's drawn part centered in the room. That's a little below the middle of the room above the caption (the ring's open bottom reaches toward the caption), so values read as centered in the panel and don't jump when the mode changes. A Gauge's ring is centered on that line. A Digital value moves only if it would leave the room. A Bar's digits and bar move up together if the bar would end up closer to the caption than to the digits; if there isn't room even for that, the two are centered as a group.

Values are centered by their digits' ink, not the font's line box, which has empty room for descenders under the digits. The phone configurator's dash mock (`platform/esp32/web/index.html`) mirrors these rules; keep it in step.

## Usage

`InfoScreen` owns `MAX_PANELS` (6) `PanelWidget` instances, sized/positioned by `compute_layout()`, and refreshes their view-models from a caller-supplied builder callback — see [gui/screens](../screens/README.md).
