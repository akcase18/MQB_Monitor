/**
 * @file uds_binding.generated.inc
 * @brief GENERATED FILE -- do not hand-edit. Produced by
 * tools/gen_catalog.py from core/vehicle/data/mqb/signals.json,
 * filtered to that registry's "active" entries only (WIP entries are
 * excluded). Edit the registry -- by hand, or via the monitor tool's
 * Registry/Learn tabs -- re-run the script, and commit both.
 *
 * Included by uds_binding.cpp inside `namespace core`, after the
 * transform_* definitions it references by name.
 *
 * PARALLEL ARRAY to this product's signal_catalog.generated.inc:
 * entry i here describes entry i there. Both come from one
 * filtered pass, so the correspondence holds by construction.
 */

const UdsSignalBinding UDS_BINDINGS[] = {
    /* Cool Temp */
    { 0,
      2000,
      { { 0xF4, 0x05, -40.0f, 1.0f }, { 0, 0, 0.0f, 0.0f } },
      1,
      transform_default },
    /* Eng Oil Temp */
    { 1,
      1000,
      { { 0x40, 0xD5, 0.0f, 0.1f }, { 0, 0, 0.0f, 0.0f } },
      1,
      transform_default },
    /* Int Air Temp */
    { 2,
      100,
      { { 0xF4, 0x0F, -40.0f, 1.0f }, { 0, 0, 0.0f, 0.0f } },
      1,
      transform_default },
    /* Charge Pres */
    { 3,
      100,
      { { 0x20, 0x2A, 0.0f, 0.001f }, { 0, 0, 0.0f, 0.0f } },
      1,
      transform_default },
    /* Int Pres */
    { 4,
      50,
      { { 0x3E, 0x70, 0.0f, 0.001f }, { 0, 0, 0.0f, 0.0f } },
      1,
      transform_default },
    /* Amb Pres */
    { 5,
      1000,
      { { 0x13, 0xCA, 0.0f, 0.0001f }, { 0, 0, 0.0f, 0.0f } },
      1,
      transform_default },
    /* Int Pres Act. */
    { 22,
      50,
      { { 0x13, 0xCA, 0.0f, 0.0001f }, { 0x3E, 0x70, 0.0f, 0.001f } },
      2,
      transform_difference },
    /* Engine Speed */
    { 7,
      50,
      { { 0x20, 0x6E, 0.0f, 1.0f }, { 0, 0, 0.0f, 0.0f } },
      1,
      transform_default },
    /* Engine Torque */
    { 8,
      50,
      { { 0x43, 0x7C, 0.0f, 0.1f }, { 0, 0, 0.0f, 0.0f } },
      1,
      transform_default },
    /* Speed */
    { 9,
      100,
      { { 0x20, 0x33, 0.0f, 0.01f }, { 0, 0, 0.0f, 0.0f } },
      1,
      transform_default },
    /* Fuel */
    { 10,
      5000,
      { { 0x22, 0x06, 0.0f, 1.0f }, { 0, 0, 0.0f, 0.0f } },
      1,
      transform_default },
    /* Fuel */
    { 11,
      5000,
      { { 0xF4, 0x2F, 0.0f, 0.39215686274509803f }, { 0, 0, 0.0f, 0.0f } },
      1,
      transform_default },
    /* Throttle Pos */
    { 12,
      50,
      { { 0xF4, 0x11, 0.0f, 0.39215686274509803f }, { 0, 0, 0.0f, 0.0f } },
      1,
      transform_default },
    /* Misfire Cnt */
    { 13,
      500,
      { { 0x29, 0x04, 0.0f, 1.0f }, { 0, 0, 0.0f, 0.0f } },
      1,
      transform_default },
    /* Misfire Cnt Cyl 1 */
    { 14,
      500,
      { { 0x29, 0x66, 0.0f, 1.0f }, { 0, 0, 0.0f, 0.0f } },
      1,
      transform_default },
    /* Misfire Cnt Cyl 2 */
    { 15,
      500,
      { { 0x29, 0x67, 0.0f, 1.0f }, { 0, 0, 0.0f, 0.0f } },
      1,
      transform_default },
    /* Misfire Cnt Cyl 3 */
    { 16,
      500,
      { { 0x29, 0x68, 0.0f, 1.0f }, { 0, 0, 0.0f, 0.0f } },
      1,
      transform_default },
    /* Misfire Cnt Cyl 4 */
    { 17,
      500,
      { { 0x29, 0x69, 0.0f, 1.0f }, { 0, 0, 0.0f, 0.0f } },
      1,
      transform_default },
    /* Time Corr. Cyl 1 */
    { 18,
      100,
      { { 0x20, 0x0A, 0.0f, 0.01f }, { 0, 0, 0.0f, 0.0f } },
      1,
      transform_default },
    /* Time Corr. Cyl 2 */
    { 19,
      100,
      { { 0x20, 0x0B, 0.0f, 0.01f }, { 0, 0, 0.0f, 0.0f } },
      1,
      transform_default },
    /* Time Corr. Cyl 3 */
    { 20,
      100,
      { { 0x20, 0x0C, 0.0f, 0.01f }, { 0, 0, 0.0f, 0.0f } },
      1,
      transform_default },
    /* Time Corr. Cyl 4 */
    { 21,
      100,
      { { 0x20, 0x0D, 0.0f, 0.01f }, { 0, 0, 0.0f, 0.0f } },
      1,
      transform_default },
};

static_assert(sizeof(UDS_BINDINGS) / sizeof(UDS_BINDINGS[0])
                  == UDS_BINDING_COUNT,
              "UDS_BINDING_COUNT disagrees with the array it "
              "sizes -- regenerate both with tools/gen_catalog.py");
