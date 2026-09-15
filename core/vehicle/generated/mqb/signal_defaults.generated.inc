/**
 * @file signal_defaults.generated.inc
 * @brief GENERATED FILE -- do not hand-edit. Produced by
 * tools/gen_catalog.py from core/vehicle/data/mqb/signals.json,
 * filtered to that registry's "active" entries only (WIP entries are
 * excluded). Edit the registry -- by hand, or via the monitor tool's
 * Registry/Learn tabs -- re-run the script, and commit both.
 *
 * Included by config_store.cpp inside `namespace core`.
 *
 * The factory panel assignment, by priority rank (0 = most
 * important), padded to MAX_PANELS. Ranks past
 * DEFAULT_NUM_PANELS are real but inactive.
 */

inline constexpr uint8_t DEFAULT_NUM_PANELS = 5;

inline constexpr PrioritizedSignal DEFAULT_PRIORITIES[] = {
    { 0, UnitSystem::Imperial } /* Cool Temp */,
    { 12, UnitSystem::Imperial } /* Throttle Pos */,
    { 2, UnitSystem::Imperial } /* Int Air Temp */,
    { 1, UnitSystem::Imperial } /* Eng Oil Temp */,
    { 22, UnitSystem::Imperial } /* Int Pres Act. */,
    { 8, UnitSystem::Imperial } /* Engine Torque */,
};

static_assert(sizeof(DEFAULT_PRIORITIES) / sizeof(DEFAULT_PRIORITIES[0])
                  == MAX_PANELS,
              "DEFAULT_PRIORITIES must cover every panel rank");
static_assert(DEFAULT_NUM_PANELS >= 1 && DEFAULT_NUM_PANELS <= MAX_PANELS,
              "DEFAULT_NUM_PANELS out of range");
