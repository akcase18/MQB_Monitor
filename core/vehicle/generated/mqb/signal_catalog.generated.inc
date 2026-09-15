/**
 * @file signal_catalog.generated.inc
 * @brief GENERATED FILE -- do not hand-edit. Produced by
 * tools/gen_catalog.py from core/vehicle/data/mqb/signals.json,
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
    /* Cool Temp */
    { 0,
      "Cool Temp",
      UnitType::Temp,
      -40.0f,
      130.0f },
    /* Eng Oil Temp */
    { 1,
      "Eng Oil Temp",
      UnitType::Temp,
      -40.0f,
      150.0f },
    /* Int Air Temp */
    { 2,
      "Int Air Temp",
      UnitType::Temp,
      -40.0f,
      80.0f },
    /* Charge Pres */
    { 3,
      "Charge Pres",
      UnitType::Pressure,
      0.0f,
      3.0f },
    /* Int Pres */
    { 4,
      "Int Pres",
      UnitType::Pressure,
      0.0f,
      3.0f },
    /* Amb Pres */
    { 5,
      "Amb Pres",
      UnitType::Pressure,
      0.8f,
      1.1f },
    /* Int Pres Act. */
    { 22,
      "Int Pres Act.",
      UnitType::Pressure,
      -1.0f,
      2.0f },
    /* Engine Speed */
    { 7,
      "Engine Speed",
      UnitType::RPM,
      0.0f,
      7000.0f },
    /* Engine Torque */
    { 8,
      "Engine Torque",
      UnitType::Torque,
      0.0f,
      500.0f },
    /* Speed */
    { 9,
      "Speed",
      UnitType::Speed,
      0.0f,
      300.0f },
    /* Fuel */
    { 10,
      "Fuel",
      UnitType::Volume,
      0.0f,
      70.0f },
    /* Fuel */
    { 11,
      "Fuel",
      UnitType::Percent,
      0.0f,
      100.0f },
    /* Throttle Pos */
    { 12,
      "Throttle Pos",
      UnitType::Percent,
      0.0f,
      100.0f },
    /* Misfire Cnt */
    { 13,
      "Misfire Cnt",
      UnitType::None,
      0.0f,
      500.0f },
    /* Misfire Cnt Cyl 1 */
    { 14,
      "Misfire Cnt Cyl 1",
      UnitType::None,
      0.0f,
      200.0f },
    /* Misfire Cnt Cyl 2 */
    { 15,
      "Misfire Cnt Cyl 2",
      UnitType::None,
      0.0f,
      200.0f },
    /* Misfire Cnt Cyl 3 */
    { 16,
      "Misfire Cnt Cyl 3",
      UnitType::None,
      0.0f,
      200.0f },
    /* Misfire Cnt Cyl 4 */
    { 17,
      "Misfire Cnt Cyl 4",
      UnitType::None,
      0.0f,
      200.0f },
    /* Time Corr. Cyl 1 */
    { 18,
      "Time Corr. Cyl 1",
      UnitType::Degrees,
      -10.0f,
      10.0f },
    /* Time Corr. Cyl 2 */
    { 19,
      "Time Corr. Cyl 2",
      UnitType::Degrees,
      -10.0f,
      10.0f },
    /* Time Corr. Cyl 3 */
    { 20,
      "Time Corr. Cyl 3",
      UnitType::Degrees,
      -10.0f,
      10.0f },
    /* Time Corr. Cyl 4 */
    { 21,
      "Time Corr. Cyl 4",
      UnitType::Degrees,
      -10.0f,
      10.0f },
};

static_assert(sizeof(SIGNAL_CATALOG) / sizeof(SIGNAL_CATALOG[0])
                  == SIGNAL_CATALOG_COUNT,
              "SIGNAL_CATALOG_COUNT disagrees with the array it "
              "sizes -- regenerate both with tools/gen_catalog.py");
