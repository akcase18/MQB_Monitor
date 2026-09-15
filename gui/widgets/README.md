# gui/widgets/

`PanelWidget` — the single reusable info-panel building block used by the Info screen — and `ToastOverlay`, the transient message layer the touchless product uses instead of navigation.

`PanelWidget` is styled purely from a `Theme` and driven purely by a plain-data `PanelViewModel`; it has no pointer into `core::vehicle`/`core::config`, so it's previewable with fabricated data independent of CAN/config wiring. [gui/bindings](../bindings/README.md) is the only place allowed to build a real `PanelViewModel` from vehicle data.

## Files

- **`inc/panel_widget.hpp`** / **`src/panel_widget.cpp`**:
  - `DisplayMode { Digital, Bar, Gauge }` — how a panel presents its value. Digital is the bare tabular number; Bar/Gauge add a fast-glance graphic *alongside* it, never instead of it.
  - `PanelViewModel { name, unit_label, value_str, large, hero, warn_lo_active, warn_hi_active, mode, range_pct }` — everything one panel needs to render. `hero` is true only for the truly roomy full-height slots (count 1's panel, count 3's hero — see [gui/screens](../screens/README.md)), giving those a bigger font/arc tier than `large` alone would. `range_pct` (0..1) is the value's position within its signal's display range, meaningful only for Bar/Gauge.
  - `PanelWidget::init(parent, theme)` — pre-creates the button plus *all three* display-mode visuals (label, `lv_bar`, `lv_arc`) up front, hidden by default. Changing a panel's mode later (rare — only via the Configurator) just shows/hides existing objects rather than allocating/freeing LVGL objects on the hot refresh path.
  - `set_view_model(vm)` — applies text, size tier, active display mode, and warning highlight (label background for Digital, bar/arc indicator color for Bar/Gauge).
  - `set_visible(visible)` — shows/hides without destroying, used when the active panel count shrinks.

  Sizing is entirely the parent's responsibility — `PanelWidget` only ever sets its own *internal* content styling (fonts/colors/text), never its own width/height. That's what lets the same widget work whether it's previewed standalone or placed by `gui::compute_layout()` (see [gui/screens](../screens/README.md)).

- **`inc/toast_overlay.hpp`** / **`src/toast_overlay.cpp`** — `ToastOverlay`, a short message shown over whatever is on screen and dismissed on a timer.

  It exists because the gauge product has one screen and no navigation, so everything a button press needs to acknowledge — brightness changed, units changed, peaks reset, warning acknowledged, and the gesture hint itself — has nowhere else to go. A screen apiece would be far more machinery than any of those deserves, and navigation is exactly what a two-button dashboard must not grow.

  - Lives on `lv_layer_top()`, not on a screen, so it survives a screen load or a full rebuild without being recreated and sits above the panel grid without being part of its layout.
  - `show(text, duration_ms)` — `text` may contain newlines, which is what makes the multi-line gesture hint the same widget rather than a second one. `duration_ms == 0` keeps it up until `hide()`, which is what an armed factory-reset confirmation wants: a countdown that dismissed itself while you were reading it would be a confirmation prompt in name only.
  - **Never clickable.** Nothing on it is a control, and a message that can be tapped is a message that can be tapped by accident — so it never intercepts input on any product, touchscreen or not.
  - One paused `lv_timer` for the widget's whole life rather than a one-shot per message: a toast is raised on every button press, and a self-deleting timer would leave a pointer here that has to be remembered not to touch.

## Usage

`InfoScreen` owns `MAX_PANELS` (6) `PanelWidget` instances, sized/positioned by `compute_layout()`, and refreshes their view-models from a caller-supplied builder callback — see [gui/screens](../screens/README.md). `ToastOverlay` is owned by the composition root instead, since what is worth saying is a product decision and there is nothing for a screen to do with it.

## The unit tag, and why it has two placements

`Theme::Metrics::unit_stacked` decides whether the unit tag sits beside the value or on its own line at the top of the panel. It is a width decision, not a taste one.

The tag is normally placed to the *right* of a value that is itself centred, so the pair overhangs the panel's centre line by half the value plus the whole tag. In a 213 px hero that is free. In the gauge product's 102 px strip column it is not: the value alone can be 90 px wide ("10.34" at `bold_30`), and "77.2 psi" rendered as "77.2 ps" with the tag walking off the edge. Stacking makes the constraint `max(value, tag)` instead of their sum.

`test_gui_theme` asserts the rule that applies to each theme rather than one rule for both — including that value + tag side by side genuinely *would not* fit on the gauge product, so the stacking is not left in place out of habit once it stops being necessary. The original fit check measured the value and stopped there, which is exactly how the clipping shipped unnoticed.
