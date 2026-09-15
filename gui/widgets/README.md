# gui/widgets/

`PanelWidget` — the single reusable info-panel building block used by the Info screen. Styled purely from a `Theme` and driven purely by a plain-data `PanelViewModel`; it has no pointer into `core::vehicle`/`core::config`, so it's previewable with fabricated data independent of CAN/config wiring. [gui/bindings](../bindings/README.md) is the only place allowed to build a real `PanelViewModel` from vehicle data.

## Files

- **`inc/panel_widget.hpp`** / **`src/panel_widget.cpp`**:
  - `DisplayMode { Digital, Bar, Gauge }` — how a panel presents its value. Digital is the bare tabular number; Bar/Gauge add a fast-glance graphic *alongside* it, never instead of it.
  - `PanelViewModel { name, unit_label, value_str, large, hero, warn_lo_active, warn_hi_active, mode, range_pct }` — everything one panel needs to render. `hero` is true only for the truly roomy full-height slots (count 1's panel, count 3's hero — see [gui/screens](../screens/README.md)), giving those a bigger font/arc tier than `large` alone would. `range_pct` (0..1) is the value's position within its signal's display range, meaningful only for Bar/Gauge.
  - `PanelWidget::init(parent, theme)` — pre-creates the button plus *all three* display-mode visuals (label, `lv_bar`, `lv_arc`) up front, hidden by default. Changing a panel's mode later (rare — only via the Configurator) just shows/hides existing objects rather than allocating/freeing LVGL objects on the hot refresh path.
  - `set_view_model(vm)` — applies text, size tier, active display mode, and warning highlight. A warning repaints the whole panel background `warn_lo`/`warn_hi`; the Bar/Gauge fill deliberately stays accent-colored (tinting it to match made the graphic vanish into its own panel), and is inset inside its track so the track color rings it — the only separation available when the user's accent is itself blue or red.
  - `set_visible(visible)` — shows/hides without destroying, used when the active panel count shrinks.

  Sizing is entirely the parent's responsibility — `PanelWidget` only ever sets its own *internal* content styling (fonts/colors/text), never its own width/height. That's what lets the same widget work whether it's previewed standalone or placed by `gui::compute_layout()` (see [gui/screens](../screens/README.md)).

## Usage

`InfoScreen` owns `MAX_PANELS` (6) `PanelWidget` instances, sized/positioned by `compute_layout()`, and refreshes their view-models from a caller-supplied builder callback — see [gui/screens](../screens/README.md).
