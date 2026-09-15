/**
 * @file curve_library.hpp
 * @brief Built-in starting points for a sensor calibration.
 *
 * A preset is a **button that fills in a SensorCal**, not a mode a channel
 * is left in. `sensor_preset_apply()` copies numbers into the struct and is
 * then finished with; no conversion anywhere reads a preset id, and nothing
 * needs the preset to still exist afterwards. Two consequences, both of
 * which are the reason it is built this way:
 *
 * - "Pick your sensor from a list" and "type in your own curve" are the
 *   same code path in the web form, so a combination -- pick the closest
 *   preset, then correct one field against your datasheet -- works without
 *   being designed for separately, and is not a second-class mode.
 * - A firmware update that adds, corrects or removes a preset cannot
 *   change what an already-configured gauge reads. The numbers live in the
 *   saved config, not in the version of the firmware that happens to be
 *   installed. On an oil pressure gauge that distinction is worth a lot.
 *
 * `SensorCal::preset_id` survives as provenance so the form can show
 * "Generic NTC 10k (edited)" rather than "Custom". Nothing computes with it.
 *
 * **On vendor curves.** This table deliberately ships no Bosch/VDO/Delphi
 * coefficients. Those parts are not interchangeable and a curve that is
 * merely close is wrong by tens of degrees at the ends -- so a number that
 * has not been read off the actual datasheet has no business in the path
 * between a temperature sender and a warning light. The right route for a
 * specific part is CurveModel::Lut with the published R/T table entered
 * from the datasheet, which needs no firmware change at all. Adding a
 * verified curve here later is one row in this table.
 */
#pragma once

#include "sensor_cal.hpp"

namespace core
{

    /** `SensorCal::preset_id` for a calibration that came from nowhere but
     * the user. Never matches a table entry. */
    constexpr uint16_t SENSOR_PRESET_CUSTOM = 0;

    struct SensorPreset
    {
            uint16_t    id;
            SensorKind  kind;
            const char *name;

            /** One line for the form, saying what still needs setting.
             * Never null. */
            const char *note;

            /** The numbers this preset writes. Held by value so applying
             * one is a copy, not a lookup that has to keep working. */
            SensorCal cal;
    };

    uint8_t             sensor_preset_count();
    const SensorPreset &sensor_preset_at(uint8_t index);

    /** @returns Null if `id` is unknown -- including SENSOR_PRESET_CUSTOM,
     * which is the point: an unrecognised id degrades to "Custom" rather
     * than to a wrong curve. */
    const SensorPreset *sensor_preset_find(uint16_t id);

    /**
     * Overwrites `cal`'s electrical description from a preset, preserving
     * the two fields that describe wiring rather than the part: `channel`
     * and `signal_id`. Sets `preset_id` to `id`.
     *
     * Filter constant and band margin are overwritten too -- a preset's
     * sensible defaults for those are part of what makes it useful, and
     * both are ordinary form fields afterwards.
     *
     * @returns False and leaves `cal` untouched if `id` is unknown.
     */
    bool sensor_preset_apply(uint16_t id, SensorCal &cal);

    /**
     * A blank but coherent calibration of the given kind -- what a channel
     * should start life as, and what "Custom" resets to. Never produces a
     * cal that reads as valid; a caller still has to fill in the numbers.
     */
    SensorCal sensor_cal_default(SensorKind kind);

} // namespace core
