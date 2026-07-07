# gui/

The LVGL presentation layer: every screen, widget, and style token in the app. `gui/widgets` and `gui/screens` have zero dependency on `core/` or `platform/` — they're driven entirely by plain-data view-model structs, which is what makes them previewable with fabricated data independent of real CAN/config wiring. `gui/bindings` is the sole exception: it's the one place allowed to depend on both `core::` and `gui::`.

## Subfolders

- **[theme/](theme/README.md)** — `Theme`: palette, typography, and metrics tokens. The one file a full reskin touches.
- **[widgets/](widgets/README.md)** — `PanelWidget`, the single reusable data-panel building block (Digital/Bar/Gauge).
- **[screens/](screens/README.md)** — every screen (Info, Configurator, Signal Picker, Warning Config, Settings, Theme Picker), the panel-layout algorithm, and shared screen-construction helpers.
- **[bindings/](bindings/README.md)** — `VehicleBinding`, the sole bridge between `core::` vehicle data and `gui::` view-models.
- **[assets/](assets/README.md)** — font source files and their licenses.

## Dependency shape

```
gui/theme    → (nothing outside gui/theme)
gui/widgets  → gui/theme
gui/screens  → gui/theme, gui/widgets
gui/bindings → gui/widgets, gui/screens, core/vehicle, core/config
```

`src/main.cpp` is the only other place that touches both `core::` and `gui::` directly — it does so for wiring/composition, not translation, which is what `gui/bindings` is for.
