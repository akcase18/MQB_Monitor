/**
 * @file signal_defaults.generated.inc
 * @brief GENERATED FILE -- do not hand-edit. Produced by
 * tools/gen_catalog.py from core/vehicle/data/gauge/signals.json,
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

inline constexpr uint8_t DEFAULT_NUM_PANELS = 3;

inline constexpr PrioritizedSignal DEFAULT_PRIORITIES[] = {
    { 0, UnitSystem::Imperial } /* Oil Pres */,
    { 1, UnitSystem::Imperial } /* Oil Temp */,
    { 2, UnitSystem::Imperial } /* Cool Temp */,
    { 0, UnitSystem::Imperial } /* Oil Pres */,
    { 1, UnitSystem::Imperial } /* Oil Temp */,
    { 2, UnitSystem::Imperial } /* Cool Temp */,
};

static_assert(sizeof(DEFAULT_PRIORITIES) / sizeof(DEFAULT_PRIORITIES[0])
                  == MAX_PANELS,
              "DEFAULT_PRIORITIES must cover every panel rank");
static_assert(DEFAULT_NUM_PANELS >= 1 && DEFAULT_NUM_PANELS <= MAX_PANELS,
              "DEFAULT_NUM_PANELS out of range");
