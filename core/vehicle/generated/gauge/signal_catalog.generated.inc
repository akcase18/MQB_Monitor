/**
 * @file signal_catalog.generated.inc
 * @brief GENERATED FILE -- do not hand-edit. Produced by
 * tools/gen_catalog.py from core/vehicle/data/gauge/signals.json,
 * filtered to that registry's "active" entries only (WIP entries are
 * excluded). Edit the registry -- by hand, or via the monitor tool's
 * Registry/Learn tabs -- re-run the script, and commit both.
 *
 * Included by signal_catalog.cpp inside `namespace core`.
 *
 * Presentation metadata only -- name, unit, fallback display
 * range. How a signal is *acquired* is deliberately not here;
 * see the product's own acquisition layer.
 */

const SignalSpec SIGNAL_CATALOG[] = {
    /* Oil Pres */
    { 0,
      "Oil Pres",
      UnitType::Pressure,
      0.0f,
      10.0f },
    /* Oil Temp */
    { 1,
      "Oil Temp",
      UnitType::Temp,
      -40.0f,
      150.0f },
    /* Cool Temp */
    { 2,
      "Cool Temp",
      UnitType::Temp,
      -40.0f,
      130.0f },
};

static_assert(sizeof(SIGNAL_CATALOG) / sizeof(SIGNAL_CATALOG[0])
                  == SIGNAL_CATALOG_COUNT,
              "SIGNAL_CATALOG_COUNT disagrees with the array it "
              "sizes -- regenerate both with tools/gen_catalog.py");
