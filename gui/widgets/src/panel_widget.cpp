#include "panel_widget.hpp"
#include "mem_placement.hpp"

#include <string.h>

namespace gui
{

    namespace
    {
        /* Sized to contain a 4-character value (the font each tier's
         * Bar/Gauge readout uses) without the digits poking out past the
         * ring — a real bug where "3212" at 40px overflowed an 80px arc. */
        constexpr lv_coord_t ARC_SIZE_SMALL = 104;
        constexpr lv_coord_t ARC_SIZE_LARGE = 128;
        constexpr lv_coord_t ARC_SIZE_HERO  = 180;
        constexpr lv_coord_t ARC_WIDTH      = 7;

        /* Both graphics draw their track wider than their fill and push the
         * fill in by the difference, leaving a strip of track color ringing
         * the fill. That strip is the only thing separating an accent-
         * colored graphic from the panel behind it when the two are the
         * same hue — which is exactly what a warning does, since a warning
         * paints the whole panel blue/red (see set_view_model) and the
         * accent is itself user-selectable to blue or red. */
        constexpr lv_coord_t INDICATOR_INSET = 2;
        constexpr lv_coord_t ARC_TRACK_WIDTH = ARC_WIDTH + 2 * INDICATOR_INSET;

        constexpr lv_coord_t BAR_HEIGHT      = 18;
        constexpr lv_coord_t BAR_HEIGHT_HERO = 28;
        constexpr lv_coord_t BAR_DATA_GAP    = 6;

        /* Every mode positions exactly one "anchor" element at the
         * button's center (offset up to leave room for the label's fixed
         * slot below); any companion element (a Bar's value, a Gauge's
         * centered value) is then placed relative to that anchor. This
         * keeps the label's position the only thing that never depends on
         * which mode/tier is active — previously the label flowed
         * directly after the graphic in a flex column, so its height
         * shifted the label around from panel to panel. */
        constexpr lv_coord_t LABEL_BOTTOM_PAD  = 4;
        constexpr lv_coord_t CONTENT_LABEL_GAP = 4;

        /* Strictly centering the content in the zone above the label (see
         * content_center_offset() below) reads as biased high — the eye
         * weighs the label's caption text as part of the panel's visual
         * mass even though it isn't part of the "content" being centered,
         * so nudge the content down a bit to compensate. */
        constexpr lv_coord_t CONTENT_VERTICAL_NUDGE = 8;

        /* The arc/bar's unfilled track needs its own visible color — left
         * at LVGL's default it rendered as a near-invisible sliver against
         * the black panel bg, indistinguishable from the (also too dark)
         * old accent color used for the filled portion. Doubles as the
         * fill's outline (see INDICATOR_INSET), so it has to stay a
         * neutral that reads against black and against a warning's
         * blue/red panel alike. */
        const lv_color_t TRACK_COLOR = lv_color_hex(0x404040);

        /* 270-degree sweep with the gap centered at the bottom (6 o'clock),
         * matching a classic analog gauge face. 0deg is right, angles
         * increase clockwise. */
        constexpr lv_value_precise_t ARC_BG_START_ANGLE = 135;
        constexpr lv_value_precise_t ARC_BG_END_ANGLE   = 45;

        /* Deliberately blind to warn_lo_active/warn_hi_active: a warning
         * already paints the entire panel that color, so tinting the fill
         * to match made the Bar/Gauge vanish into its own background for
         * as long as the warning lasted. */
        lv_color_t indicator_color(const Theme &theme, const PanelViewModel &vm)
        {
            /* range_pct is the raw (unclamped) ratio against the signal's
             * display_min/max — outside [0,1] means the value has pegged
             * the graphic at 100%/0% full, which reads identically to
             * "exactly at max/min" unless tinted differently. A signal can
             * peg without ever crossing a configured warning threshold. */
            if (vm.range_pct > 1.0f || vm.range_pct < 0.0f)
            {
                return theme.palette.range_overflow;
            }
            return theme.palette.accent;
        }

        uint8_t range_pct_to_100(float range_pct)
        {
            if (range_pct < 0.0f)
            {
                range_pct = 0.0f;
            }
            else if (range_pct > 1.0f)
            {
                range_pct = 1.0f;
            }
            return static_cast<uint8_t>(range_pct * 100.0f);
        }

        /* Bar/Gauge already give an at-a-glance read via the graphic
         * itself — the value next to/inside it is there for a closer
         * look, not to be the dominant element, so a hero slot only
         * bumps it to data_hero, not the much bigger data_mega. Digital
         * has no ring to fit inside, so a hero slot's bare number gets to
         * be as big as the panel can hold. */
        const lv_font_t *value_font(const Typography     &type,
                                    const PanelViewModel &vm)
        {
            if (vm.hero)
            {
                return vm.mode == DisplayMode::Digital ? type.data_mega
                                                       : type.data_hero;
            }
            if (vm.mode != DisplayMode::Digital)
            {
                return type.data_sm;
            }
            return vm.large ? type.data_lg : type.data_sm;
        }

        /* How far to shift the content "anchor" up from dead-center so it
         * clears the label's reserved slot below — derived from the
         * caption font actually in use rather than a guessed constant, so
         * it can't drift out of sync if label sizes ever change. */
        lv_coord_t content_center_offset(const lv_font_t *caption_font)
        {
            lv_coord_t label_zone = lv_font_get_line_height(caption_font)
                                    + LABEL_BOTTOM_PAD + CONTENT_LABEL_GAP;
            return -(label_zone / 2) + CONTENT_VERTICAL_NUDGE;
        }
    } // namespace

    FLASHMEM void PanelWidget::init(lv_obj_t *parent, const Theme &theme)
    {
        theme_ = &theme;

        btn_ = lv_btn_create(parent);
        lv_obj_remove_style_all(btn_);
        /* Panels cover nearly the whole screen, so without this a swipe
         * gesture starting on one (i.e. almost anywhere) would never reach
         * the screen's own gesture handler — bubbling is opt-in per object,
         * not a default. */
        lv_obj_set_gesture_bubble(btn_, true);
        lv_obj_set_style_bg_color(btn_, theme.palette.bg, 0);
        lv_obj_set_style_bg_opa(btn_, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(btn_, theme.palette.fg, 0);
        lv_obj_set_style_radius(btn_, theme.metrics.panel_radius, 0);
        lv_obj_set_style_shadow_width(btn_, 0, 0);
        /* Panels share the screen's own bg color, so without an outline
         * a panel's bounds are only implied by the gap to its neighbors —
         * this draws the actual card edge. */
        lv_obj_set_style_border_width(
            btn_, theme.metrics.panel_border_width, 0);
        lv_obj_set_style_border_color(btn_, theme.palette.panel_border, 0);
        lv_obj_set_style_border_opa(btn_, LV_OPA_COVER, 0);
        /* Darkened rather than the raw accent — accent is now a bright,
         * saturated color tuned for bar/arc fill visibility, and flashing
         * that at full strength across the whole panel on every tap read
         * as a jarring flash rather than subtle press feedback. */
        lv_obj_set_style_bg_color(
            btn_, lv_color_darken(theme.palette.accent, 180), LV_STATE_PRESSED);
        lv_obj_set_style_pad_all(btn_, 4, 0);

        /* No flex here on purpose: every child is positioned explicitly in
         * set_view_model() via lv_obj_align()/lv_obj_align_to(), so the
         * label's position never depends on the graphic/value's height
         * the way it would flowing through a flex column. */
        arc_ = lv_arc_create(btn_);
        lv_arc_set_bg_angles(arc_, ARC_BG_START_ANGLE, ARC_BG_END_ANGLE);
        lv_arc_set_range(arc_, 0, 100);
        lv_obj_remove_style(arc_, nullptr, LV_PART_KNOB | LV_STATE_ANY);
        lv_obj_set_clickable(arc_, false);
        lv_obj_set_style_arc_width(arc_, ARC_TRACK_WIDTH, LV_PART_MAIN);
        lv_obj_set_style_arc_width(arc_, ARC_WIDTH, LV_PART_INDICATOR);
        /* Both parts share an outer radius, so without this the fill would
         * sit flush against the track's outer edge and only be ringed on
         * the inside. */
        lv_obj_set_style_pad_all(arc_, INDICATOR_INSET, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(arc_, TRACK_COLOR, LV_PART_MAIN);
        lv_obj_set_hidden(arc_, true);

        data_ = lv_label_create(btn_);
        lv_obj_set_style_text_color(data_, theme.palette.fg, 0);
        lv_obj_set_style_bg_opa(data_, LV_OPA_TRANSP, 0);

        /* Unit sits beside the value as a small superscript-style tag
         * (e.g. "72" + a smaller "°F") rather than folded into the name
         * caption below — keeping the caption to just the signal's name
         * is what stops it from wrapping in narrow panels. Hidden for
         * Gauge, whose value already has to fit inside a fixed-size ring. */
        unit_ = lv_label_create(btn_);
        lv_obj_set_style_text_color(unit_, theme.palette.fg, 0);
        lv_obj_set_style_bg_opa(unit_, LV_OPA_TRANSP, 0);
        /* Typography::unit is the same font at every tier and mode (see
         * theme.hpp), so it belongs here rather than being re-set on
         * every set_view_model() call. */
        lv_obj_set_style_text_font(unit_, theme.typography.unit, 0);

        bar_ = lv_bar_create(btn_);
        lv_bar_set_range(bar_, 0, 100);
        lv_obj_set_style_bg_color(bar_, TRACK_COLOR, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(bar_, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_pad_all(bar_, INDICATOR_INSET, LV_PART_MAIN);
        lv_obj_set_style_bg_color(
            bar_, theme.palette.accent, LV_PART_INDICATOR);
        lv_obj_set_hidden(bar_, true);

        /* Fixed to the bottom of the panel regardless of mode/tier — the
         * one thing that must never move. Wraps to 2 lines (rather than
         * clipping/running past the edge) for names too long to fit a
         * small panel's width at 1 line. */
        label_ = lv_label_create(btn_);
        lv_obj_set_style_text_color(label_, theme.palette.fg, 0);
        lv_label_set_long_mode(label_, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_style_text_align(label_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(label_, LV_PCT(100));
        lv_obj_align(label_, LV_ALIGN_BOTTOM_MID, 0, -LABEL_BOTTOM_PAD);
    }

    void PanelWidget::set_view_model(const PanelViewModel &vm)
    {
        const Typography &type = theme_->typography;

        /* Everything below is gated on what actually moved since the last
         * call. This runs for every visible panel several times a second,
         * and LVGL's setters do no such check of their own -- each one
         * invalidates the style cache and marks the panel dirty for
         * redraw even when handed the value it already holds. In the
         * steady state only value_str changes, so the common path is now
         * one label update plus the geometry that depends on its width,
         * instead of the whole ~20-call rebuild. */
        const bool first = !applied_valid_;
        const bool mode_changed = first || vm.mode != applied_.mode;
        const bool tier_changed
            = first || vm.large != applied_.large || vm.hero != applied_.hero;
        const bool value_changed
            = first || strcmp(vm.value_str, applied_.value_str) != 0;
        const bool name_changed
            = first || strcmp(vm.name, applied_.name) != 0;
        const bool unit_changed
            = first || strcmp(vm.unit_label, applied_.unit_label) != 0;
        const bool warn_changed
            = first || vm.warn_lo_active != applied_.warn_lo_active
              || vm.warn_hi_active != applied_.warn_hi_active;
        const bool pct_changed = first || vm.range_pct != applied_.range_pct;

        if (mode_changed || tier_changed)
        {
            lv_obj_set_style_text_font(data_, value_font(type, vm), 0);
        }
        if (value_changed)
        {
            lv_label_set_text(data_, vm.value_str);
        }

        const lv_font_t *caption_font
            = vm.large ? type.label_lg : type.label_sm;
        if (tier_changed)
        {
            lv_obj_set_style_text_font(label_, caption_font, 0);
        }
        if (name_changed)
        {
            lv_label_set_text(label_, vm.name);
        }
        if (unit_changed)
        {
            lv_label_set_text(unit_, vm.unit_label);
        }

        if (mode_changed || unit_changed)
        {
            /* Gauge hides the unit outright rather than risk it — the
             * ring's size already only has room for the bare value (see
             * ARC_SIZE_* above). Digital/Bar have the horizontal room to
             * spare. */
            lv_obj_set_hidden(unit_,
                              vm.mode == DisplayMode::Gauge
                                  || vm.unit_label[0] == '\0');
        }

        /* The graphic's own placement depends only on mode/tier, but
         * data_ and unit_ are positioned with lv_obj_align_to(), which
         * resolves to a fixed offset against the base's size at the
         * moment it's called rather than tracking it — so a value_str
         * whose rendered width changed has to re-run this too, or the
         * unit tag drifts away from (or into) the number. */
        if (mode_changed || tier_changed || value_changed)
        {
            uint8_t    pct    = range_pct_to_100(vm.range_pct);
            lv_coord_t offset = content_center_offset(caption_font);

            lv_obj_set_hidden(arc_, vm.mode != DisplayMode::Gauge);
            lv_obj_set_hidden(bar_, vm.mode != DisplayMode::Bar);

            switch (vm.mode)
            {
                case DisplayMode::Bar: {
                    lv_coord_t bar_h = vm.hero ? BAR_HEIGHT_HERO : BAR_HEIGHT;
                    lv_obj_set_size(bar_, LV_PCT(80), bar_h);
                    lv_bar_set_value(bar_, pct, LV_ANIM_OFF);

                    /* The value sits above the bar, so centering the bar
                     * alone at `offset` would leave the (value + bar)
                     * pair sitting higher than intended — shift the bar
                     * down by half the pair's combined height so the pair
                     * as a whole lands at `offset`, matching Digital/
                     * Gauge's single-element look. */
                    lv_coord_t data_h
                        = lv_font_get_line_height(value_font(type, vm));
                    lv_coord_t pair_shift = (data_h + BAR_DATA_GAP) / 2;
                    lv_obj_align(bar_, LV_ALIGN_CENTER, 0, offset + pair_shift);

                    lv_obj_align_to(
                        data_, bar_, LV_ALIGN_OUT_TOP_MID, 0, -BAR_DATA_GAP);
                    lv_obj_align_to(unit_, data_, LV_ALIGN_OUT_RIGHT_TOP, 3, 2);
                    break;
                }

                case DisplayMode::Gauge: {
                    lv_coord_t size = vm.hero    ? ARC_SIZE_HERO
                                      : vm.large ? ARC_SIZE_LARGE
                                                 : ARC_SIZE_SMALL;
                    lv_obj_set_size(arc_, size, size);
                    lv_obj_align(arc_, LV_ALIGN_CENTER, 0, offset);
                    lv_arc_set_value(arc_, pct);

                    /* The value sits inside the ring rather than beside
                     * it — centering it directly on the arc keeps the
                     * "graphic + number together" rule from adding a
                     * third vertical element to position. */
                    lv_obj_align_to(data_, arc_, LV_ALIGN_CENTER, 0, 0);
                    break;
                }

                case DisplayMode::Digital:
                default:
                    lv_obj_align(data_, LV_ALIGN_CENTER, 0, offset);
                    lv_obj_align_to(unit_, data_, LV_ALIGN_OUT_RIGHT_TOP, 3, 2);
                    break;
            }
        }
        else if (pct_changed)
        {
            /* Same mode and tier, only the fill moved — update the
             * graphic's value without redoing any of the geometry above. */
            uint8_t pct = range_pct_to_100(vm.range_pct);
            if (vm.mode == DisplayMode::Bar)
            {
                lv_bar_set_value(bar_, pct, LV_ANIM_OFF);
            }
            else if (vm.mode == DisplayMode::Gauge)
            {
                lv_arc_set_value(arc_, pct);
            }
        }

        /* indicator_color() varies only with whether range_pct has pegged
         * outside [0,1]. */
        if (mode_changed || pct_changed)
        {
            lv_color_t indicator = indicator_color(*theme_, vm);
            if (vm.mode == DisplayMode::Bar)
            {
                lv_obj_set_style_bg_color(bar_, indicator, LV_PART_INDICATOR);
            }
            else if (vm.mode == DisplayMode::Gauge)
            {
                lv_obj_set_style_arc_color(arc_, indicator, LV_PART_INDICATOR);
            }
        }

        if (warn_changed)
        {
            /* A warning shades the whole panel rather than just tinting
             * the active visual — a bare colored number/bar/arc is easy
             * to miss at a glance next to five other panels; the panel's
             * entire card flipping to blue/red isn't. Digital has no
             * separate indicator to color at all, so this is its only
             * warning signal. */
            lv_color_t panel_bg = theme_->palette.bg;
            if (vm.warn_hi_active)
            {
                panel_bg = theme_->palette.warn_hi;
            }
            else if (vm.warn_lo_active)
            {
                panel_bg = theme_->palette.warn_lo;
            }
            lv_obj_set_style_bg_color(btn_, panel_bg, 0);
        }

        applied_       = vm;
        applied_valid_ = true;
    }

    void PanelWidget::set_visible(bool visible)
    {
        lv_obj_set_hidden(btn_, !visible);

        /* This is only ever called from InfoScreen::init()/rebuild(), i.e.
         * exactly when the panel is about to be given a new grid cell (and
         * therefore a new size). The lv_obj_align_to() placements in
         * set_view_model() are resolved against the sizes in effect when
         * they run, so they don't survive a resize — drop the cached
         * view-model to force the next refresh to re-apply everything
         * against the new geometry. */
        applied_valid_ = false;
    }

} // namespace gui
