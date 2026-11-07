/**
 * @file screen_helpers.hpp
 * @brief Small pieces shared by every gui/screens screen: a full-size,
 * Theme-backgrounded top-level screen object, a nav bar (title + top-
 * right close button), the horizontal dividers that separate stacked sections,
 * and the two recurring row shapes (a tappable list row, a flat checkable
 * button matrix).
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

    /**
     * A new top-level (parentless) screen object sized/colored from `theme`.
     */
    lv_obj_t *create_screen(const Theme &theme);

    /**
     * A borderless, shadowless top-right X close button.
     */
    DoneButton create_close_button(lv_obj_t *parent, const Theme &theme);

    /**
     * A full-width, theme-colored 2px horizontal divider at `y`.
     */
    lv_obj_t *create_divider(lv_obj_t    *parent,
                             const Theme &theme,
                             lv_coord_t   y);

    /**
     * The top-left title label of a screen's nav bar, in the theme's
     * title font.
     */
    lv_obj_t *create_nav_title(lv_obj_t    *parent,
                               const Theme &theme,
                               const char  *text);

    /**
     * A tappable settings-list row: label on the left, chevron on the
     * right, transparent at rest and accent-tinted while pressed.
     */
    struct ListRow
    {
            lv_obj_t *btn;
            lv_obj_t *label;
            lv_obj_t *chevron;
    };

    /**
     * Creates a ListRow under `parent` carrying `label_text`.
     */
    ListRow create_list_row(lv_obj_t    *parent,
                            const Theme &theme,
                            const char  *label_text);

    constexpr lv_coord_t MATRIX_ITEM_PAD_KEEP = -1;

    /**
     * Restyles a checkable lv_buttonmatrix to match the style used everywhere
     * else.
     *
     * @param item_pad Inner padding on each button (LV_PART_ITEMS), or
     *                 MATRIX_ITEM_PAD_KEEP to leave LVGL's own default in
     *                 place.
     */
    void style_flat_matrix(lv_obj_t    *matrix,
                           const Theme &theme,
                           lv_coord_t   item_pad);

    struct ConfirmPopup
    {
            lv_obj_t *mbox;
            lv_obj_t *cancel_btn;
            lv_obj_t *confirm_btn;
    };

    /**
     * Creates a Cancel/Confirm popup on top of the current screen.
     *
     * @param confirm_text What the committing button says.
     * @param destructive  Paints Confirm with the theme's warn_hi instead
     *                     of its accent.
     */
    ConfirmPopup create_confirm_popup(const Theme &theme,
                                      const char  *title,
                                      const char  *body,
                                      const char  *confirm_text,
                                      bool         destructive = false);

    /**
     * A small countdown ring in the top-right of the popup.
     */
    lv_obj_t *add_popup_countdown(const ConfirmPopup &popup,
                                  const Theme        &theme,
                                  int32_t             range);

    /**
     * A status tile: fixed caption above, value below.
     */
    lv_obj_t *create_status_tile(lv_obj_t    *parent,
                                 const Theme &theme,
                                 const char  *caption,
                                 lv_coord_t   x,
                                 lv_coord_t   y,
                                 lv_coord_t   width,
                                 lv_coord_t   height);

} // namespace gui
