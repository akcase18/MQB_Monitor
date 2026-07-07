/**
 * @file screen_helpers.hpp
 * @brief Small pieces shared by every gui/screens screen: a full-size,
 * Theme-backgrounded top-level screen object, and the bottom-right
 * checkmark "done" button every screen but InfoScreen uses to close.
 */
#pragma once

#include <lvgl.h>

#include "theme.hpp"

namespace gui
{

    struct DoneButton
    {
            lv_obj_t *btn;
            lv_obj_t *label;
    };

    /** A new top-level (parentless) screen object sized/colored from `theme`.
     */
    lv_obj_t *create_screen(const Theme &theme);

    /** A bottom-right checkmark button, styled from `theme`, added to `parent`.
     */
    DoneButton create_done_button(lv_obj_t *parent, const Theme &theme);

    /** A borderless, shadowless top-right X close button — the exit control
     * WarningConfigScreen/SettingsScreen/ConfiguratorScreen/
     * SignalPickerScreen use instead of the bottom-right checkmark, freeing
     * the rest of the screen for a nav bar (title + this button) over
     * stacked, divider-separated sections. */
    DoneButton create_close_button(lv_obj_t *parent, const Theme &theme);

    /** A full-width, theme-colored 2px horizontal rule at `y` — separates
     * the stacked sections under a nav bar. */
    lv_obj_t *create_divider(lv_obj_t    *parent,
                             const Theme &theme,
                             lv_coord_t   y);

} // namespace gui
