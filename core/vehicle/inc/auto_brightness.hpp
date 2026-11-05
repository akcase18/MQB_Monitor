/**
 * @file auto_brightness.hpp
 * @brief The backlight level for core::BrightnessMode::Auto: the
 * instrument cluster's own dimming level, so the dash dims with the car's
 * dimmer knob. Ramped, so a knob step glides rather than jumps, and with a
 * fallback level for when the cluster isn't answering (ignition off, the
 * module asleep, or a bench with no car). The Settings slider shifts the
 * level up or down by up to MAX_SHIFT_PCT, for a dash that should sit a
 * little brighter or dimmer than the cluster.
 *
 * The reading is Dim Ctrl, an internal signal (signals.json's "internal"
 * status): compiled in and polled while Auto is on, but never offered to
 * the user as a panel. Day/night switching from the ambient light sensor
 * isn't part of this yet.
 */
#pragma once

#include <stdint.h>

#include "signal_store.hpp"

namespace core
{

    class AutoBrightness
    {
        public:
            /**
             * signals.json's Dim Ctrl: DID 11 00 on the cluster (714/77E),
             * byte 2 -- the needle/gauge dimming curve, which follows the
             * dimmer knob.
             */
            static constexpr uint16_t DIM_SIGNAL_ID = 61;

            /**
             * No fresh reading for this long and the fallback level takes
             * over. Past SignalPoller::BACKOFF_PERIOD_MS plus Dim Ctrl's
             * cycle, so a cluster that missed a few answers and dropped to
             * the slow retry isn't mistaken for one that's gone.
             */
            static constexpr uint32_t STALE_MS = 5000;

            /**
             * How fast the level moves, in percent per second: the full
             * range in a second.
             */
            static constexpr uint32_t RAMP_PCT_PER_S = 100;

            /**
             * The furthest the slider shifts the level, either way, in
             * backlight percent.
             */
            static constexpr float MAX_SHIFT_PCT = 25.0f;

            /**
             * The shift a Settings::auto_brightness_pct slider position
             * asks for: 0 at the midpoint (50), +-MAX_SHIFT_PCT at the
             * ends, linear between.
             */
            static float shift_for(uint8_t slider_pct);

            /**
             * The backlight percent a Dim Ctrl reading asks for. Dim Ctrl
             * is a percent in a byte that can hold up to 255, so it's
             * clamped to 0-100 first, then shifted by `shift_pct`, then
             * clamped to 0-100 again. 0% isn't off:
             * IDisplay::set_backlight() floors it at the dimmest visible
             * duty.
             */
            static uint8_t level_for(float dim_pct, float shift_pct = 0.0f);

            /**
             * Starts the ramp from `pct`, what the panel shows now --
             * called on switching to Auto, so the first move starts where
             * the panel is rather than from wherever the last Auto
             * session ended.
             */
            void start(uint8_t pct, uint32_t now_ms);

            /**
             * Advances the ramp to `now_ms`.
             *
             * @param dim Dim Ctrl's current value.
             * @param shift_pct shift_for() the slider -- applied to `dim`,
             * not to the fallback.
             * @param fallback_pct The level to head for while `dim` has no
             * fresh reading (see STALE_MS).
             * @param snap Jump straight to the target instead of ramping --
             * for while the panel is dark, so it lights at the right level
             * rather than lighting bright and fading down.
             * @returns The level to show now.
             */
            uint8_t update(const SignalValue &dim,
                           float              shift_pct,
                           uint8_t            fallback_pct,
                           uint32_t           now_ms,
                           bool               snap = false);

        private:
            float    level_   = 100.0f;
            uint32_t last_ms_ = 0;
    };

} // namespace core
