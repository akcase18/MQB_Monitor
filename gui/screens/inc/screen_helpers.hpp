/**
 * @file screen_helpers.hpp
 * @brief Small pieces shared by every gui/screens screen: a full-size,
 * Theme-backgrounded top-level screen object, the nav bar (title + top-
 * right close button) every screen but InfoScreen wears, the horizontal
 * rules that separate its stacked sections, and the two recurring row
 * shapes (a tappable list row, a flat checkable button matrix).
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

    /** A borderless, shadowless top-right X close button — the exit control
     * every screen but InfoScreen uses, freeing the rest of the screen for
     * a nav bar (title + this button) over stacked, divider-separated
     * sections. */
    DoneButton create_close_button(lv_obj_t *parent, const Theme &theme);

    /** A full-width, theme-colored 2px horizontal rule at `y` — separates
     * the stacked sections under a nav bar. */
    lv_obj_t *create_divider(lv_obj_t    *parent,
                             const Theme &theme,
                             lv_coord_t   y);

    /** The top-left title label of a screen's nav bar, in the theme's
     * title font. Pass `text == nullptr` to leave it empty for a caller
     * that fills it in per-open (WarningConfigScreen shows the signal's
     * name); the returned handle is only needed in that case. */
    lv_obj_t *create_nav_title(lv_obj_t    *parent,
                               const Theme &theme,
                               const char  *text);

    /** A tappable settings-list row: label on the left, chevron on the
     * right, transparent at rest and accent-tinted while pressed. */
    struct ListRow
    {
            lv_obj_t *btn;
            lv_obj_t *label;
            lv_obj_t *chevron;
    };

    /**
     * Creates a ListRow under `parent` carrying `label_text`. Sizing,
     * positioning and the click handler are the caller's job — rows differ
     * per screen and per orientation, but their styling never does.
     */
    ListRow create_list_row(lv_obj_t    *parent,
                            const Theme &theme,
                            const char  *label_text);

    /**
     * Restyles a checkable lv_buttonmatrix away from LVGL's default
     * light-mode card+shadow look to match the flat, bordered,
     * accent-on-checked treatment used everywhere else in this app.
     *
     * @param item_pad Inner padding on each button (LV_PART_ITEMS), or
     *                 MATRIX_ITEM_PAD_KEEP to leave LVGL's own default in
     *                 place. ConfiguratorScreen's Units/Mode matrices want
     *                 explicit breathing room around their word labels;
     *                 SettingsScreen's six single-digit panel-count
     *                 buttons are sized by the matrix itself and keep the
     *                 default.
     */
    constexpr lv_coord_t MATRIX_ITEM_PAD_KEEP = -1;

    void style_flat_matrix(lv_obj_t    *matrix,
                           const Theme &theme,
                           lv_coord_t   item_pad);

} // namespace gui
