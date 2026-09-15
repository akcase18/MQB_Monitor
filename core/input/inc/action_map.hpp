/**
 * @file action_map.hpp
 * @brief Which gesture on which button does what -- as data, not as a
 * switch statement.
 *
 * Three layers, so that adding a gesture, adding an action, and deciding
 * which triggers which are three separate changes:
 *
 *   IButtons  ->  ButtonGesture/ButtonInput  ->  ActionMap  ->  handlers
 *   (raw level)      (what was performed)      (what it means) (what it does)
 *
 * and only the third is something a user does. **Remapping needs no
 * firmware change**: this is a flat array of three-byte bindings, sized to
 * go straight into the persisted config and be edited from the web form,
 * exactly like core::SensorCal. Adding an *action* is one enum value plus
 * one handler; adding a *binding* is a form entry.
 *
 * Defaults are only defaults. Nothing in §6.2 of the build plan is a
 * commitment once this is editable.
 */
#pragma once

#include <stdint.h>

#include "button_gesture.hpp"

namespace core
{

    enum class InputAction : uint8_t
    {
        None = 0,

        /** 3-up -> each signal full-screen -> back. Replaces the old
         * "promote to hero", which equal-thirds left nothing to promote
         * to. */
        CycleZoom,

        /** Which signal the zoom cycle starts on. */
        CycleZoomStart,

        ToggleDayNightBrightness,

        /** Continuous while held. Floored above zero -- no gesture may
         * leave the screen unreadable while driving. */
        RampBrightness,

        ResetPeakHold,
        ToggleUnits,
        ToggleWifiAp,
        ToggleLogging,

        /** Shown transiently on any press, as a reminder rather than a
         * manual. */
        ShowGestureHint,

        AcknowledgeWarning,

        /** Two-step on purpose. Arm starts an on-screen countdown; commit
         * is what actually wipes. Releasing the chord cancels, which the
         * composition root sees via ButtonInput::chord_active(). */
        FactoryResetArm,
        FactoryResetCommit
    };

    /** Static storage, never null. For the web form's gesture editor. */
    const char *input_action_name(InputAction action);

    struct ActionBinding
    {
            uint8_t     source; /* button index, or INPUT_SOURCE_CHORD */
            ButtonEvent event;
            InputAction action;
    };

    class ActionMap
    {
        public:
            /** Three bytes a binding; sixteen is far more than the default
             * map needs and still only 48 bytes of config. */
            static constexpr uint8_t MAX_BINDINGS = 16;

            /**
             * Installs the build plan's default map.
             *
             * @param button_count How many buttons the board actually has.
             * With one, the chord bindings are dropped and the map is
             * re-shaped rather than truncated -- see the README for what
             * a single button cannot reach.
             */
            void set_defaults(uint8_t button_count);

            void clear();

            /**
             * Replaces the whole map with `count` saved bindings.
             *
             * The read side of what `binding_count()`/`binding_at()`
             * write: core::DeviceConfig persists the table verbatim, so a
             * remap survives a power cycle without anything in between
             * having to understand what a binding means.
             *
             * Goes through bind(), so a saved table containing a duplicate
             * (source, event) collapses rather than occupying two slots,
             * and anything past MAX_BINDINGS is dropped instead of
             * overrunning. A blob is data, not a caller that can be
             * trusted to have been well-formed.
             */
            void load(const ActionBinding *bindings, uint8_t count);

            /** Replaces any existing binding for the same (source, event).
             * @returns False if the table is full. */
            bool bind(uint8_t source, ButtonEvent event, InputAction action);

            /**
             * @param warning_latched When true, any *discrete* press on
             * any source resolves to AcknowledgeWarning regardless of what
             * is mapped -- a driver reaching for the dashboard because
             * something lit up should not have to know which button
             * dismisses it. HoldRepeat and LongRelease are deliberately
             * excluded: acknowledging once every 150 ms is not a gesture.
             */
            InputAction resolve(uint8_t     source,
                                ButtonEvent event,
                                bool        warning_latched = false) const;

            uint8_t              binding_count() const;
            const ActionBinding &binding_at(uint8_t index) const;

        private:
            ActionBinding bindings_[MAX_BINDINGS];
            uint8_t       count_ = 0;
    };

} // namespace core
