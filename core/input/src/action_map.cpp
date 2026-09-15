#include "action_map.hpp"

#include "button_input.hpp"

namespace core
{

    namespace
    {
        bool is_discrete_press(ButtonEvent event)
        {
            switch (event)
            {
                case ButtonEvent::ShortPress:
                case ButtonEvent::DoublePress:
                case ButtonEvent::LongPress:
                case ButtonEvent::VeryLongPress:
                    return true;
                default:
                    return false;
            }
        }
    } // namespace

    const char *input_action_name(InputAction action)
    {
        switch (action)
        {
            case InputAction::None:
                return "none";
            case InputAction::CycleZoom:
                return "cycle zoom";
            case InputAction::CycleZoomStart:
                return "cycle zoom start";
            case InputAction::ToggleDayNightBrightness:
                return "day / night brightness";
            case InputAction::RampBrightness:
                return "ramp brightness";
            case InputAction::ResetPeakHold:
                return "reset peak hold";
            case InputAction::ToggleUnits:
                return "metric / imperial";
            case InputAction::ToggleWifiAp:
                return "wi-fi on / off";
            case InputAction::ToggleLogging:
                return "logging on / off";
            case InputAction::ShowGestureHint:
                return "show gesture hint";
            case InputAction::AcknowledgeWarning:
                return "acknowledge warning";
            case InputAction::FactoryResetArm:
                return "factory reset (arm)";
            case InputAction::FactoryResetCommit:
                return "factory reset (commit)";
        }
        return "unknown";
    }

    void ActionMap::clear()
    {
        count_ = 0;
    }

    bool ActionMap::bind(uint8_t source, ButtonEvent event, InputAction action)
    {
        for (uint8_t i = 0; i < count_; i++)
        {
            if (bindings_[i].source == source && bindings_[i].event == event)
            {
                bindings_[i].action = action;
                return true;
            }
        }

        if (count_ >= MAX_BINDINGS)
        {
            return false;
        }

        bindings_[count_].source = source;
        bindings_[count_].event  = event;
        bindings_[count_].action = action;
        count_++;
        return true;
    }

    void ActionMap::load(const ActionBinding *bindings, uint8_t count)
    {
        clear();

        if (bindings == nullptr)
        {
            return;
        }

        for (uint8_t i = 0; i < count; i++)
        {
            bind(bindings[i].source, bindings[i].event, bindings[i].action);
        }
    }

    void ActionMap::set_defaults(uint8_t button_count)
    {
        clear();

        if (button_count == 0)
        {
            return;
        }

        if (button_count == 1)
        {
            /**
             * Four gestures for rather more than four actions, so this is
             * a re-shaping rather than a truncation of the two-button map:
             * the ramp subsumes the day/night toggle, and Wi-Fi moves to
             * the one gesture nobody hits by accident. Peak-hold reset and
             * factory reset are simply not reachable -- see the README. */
            bind(0, ButtonEvent::ShortPress, InputAction::CycleZoom);
            bind(0, ButtonEvent::DoublePress, InputAction::ToggleUnits);
            bind(0, ButtonEvent::LongPress, InputAction::RampBrightness);
            bind(0, ButtonEvent::HoldRepeat, InputAction::RampBrightness);
            bind(0, ButtonEvent::VeryLongPress, InputAction::ToggleWifiAp);
            return;
        }

        /* Button A -- the one that gets used while moving. */
        bind(0, ButtonEvent::ShortPress, InputAction::CycleZoom);
        bind(
            0, ButtonEvent::DoublePress, InputAction::ToggleDayNightBrightness);
        bind(0, ButtonEvent::LongPress, InputAction::RampBrightness);
        bind(0, ButtonEvent::HoldRepeat, InputAction::RampBrightness);

        /* Button B -- the one that changes how things are shown. */
        bind(1, ButtonEvent::ShortPress, InputAction::ResetPeakHold);
        bind(1, ButtonEvent::DoublePress, InputAction::ToggleUnits);
        bind(1, ButtonEvent::LongPress, InputAction::ToggleWifiAp);

        /* Both together, two-step. */
        bind(INPUT_SOURCE_CHORD,
             ButtonEvent::LongPress,
             InputAction::FactoryResetArm);
        bind(INPUT_SOURCE_CHORD,
             ButtonEvent::VeryLongPress,
             InputAction::FactoryResetCommit);
    }

    InputAction ActionMap::resolve(uint8_t     source,
                                   ButtonEvent event,
                                   bool        warning_latched) const
    {
        if (warning_latched && is_discrete_press(event))
        {
            return InputAction::AcknowledgeWarning;
        }

        for (uint8_t i = 0; i < count_; i++)
        {
            if (bindings_[i].source == source && bindings_[i].event == event)
            {
                return bindings_[i].action;
            }
        }
        return InputAction::None;
    }

    uint8_t ActionMap::binding_count() const
    {
        return count_;
    }

    const ActionBinding &ActionMap::binding_at(uint8_t index) const
    {
        static const ActionBinding s_none { 0,
                                            ButtonEvent::None,
                                            InputAction::None };
        if (index >= count_)
        {
            return s_none;
        }
        return bindings_[index];
    }

} // namespace core
