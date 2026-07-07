#include "panel_widget.hpp"

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
        constexpr lv_coord_t ARC_WIDTH      = 9;

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
         * old accent color used for the filled portion. */
        const lv_color_t TRACK_COLOR = lv_color_hex(0x404040);

        /* 270-degree sweep with the gap centered at the bottom (6 o'clock),
         * matching a classic analog gauge face. 0deg is right, angles
         * increase clockwise. */
        constexpr lv_value_precise_t ARC_BG_START_ANGLE = 135;
        constexpr lv_value_precise_t ARC_BG_END_ANGLE   = 45;

        lv_color_t indicator_color(const Theme &theme, const PanelViewModel &vm)
        {
            if (vm.warn_hi_active)
            {
                return theme.palette.warn_hi;
            }
            if (vm.warn_lo_active)
            {
                return theme.palette.warn_lo;
            }
            /* range_pct is the raw (unclamped) ratio against the signal's
             * display_min/max — outside [0,1] means the value has pegged
             * the graphic at 100%/0% full, which reads identically to
             * "exactly at max/min" unless tinted differently. This is
             * independent of warn_hi/warn_lo: a signal can be pegged
             * without ever crossing a configured warning threshold. */
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

    void PanelWidget::init(lv_obj_t *parent, const Theme &theme)
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
        lv_obj_set_style_arc_width(arc_, ARC_WIDTH, LV_PART_MAIN);
        lv_obj_set_style_arc_width(arc_, ARC_WIDTH, LV_PART_INDICATOR);
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

        bar_ = lv_bar_create(btn_);
        lv_bar_set_range(bar_, 0, 100);
        lv_obj_set_style_bg_color(bar_, TRACK_COLOR, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(bar_, LV_OPA_COVER, LV_PART_MAIN);
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

        lv_obj_set_style_text_font(data_, value_font(type, vm), 0);
        lv_label_set_text(data_, vm.value_str);
        lv_obj_set_style_bg_opa(data_, LV_OPA_TRANSP, 0);

        const lv_font_t *caption_font
            = vm.large ? type.label_lg : type.label_sm;
        lv_obj_set_style_text_font(label_, caption_font, 0);
        lv_label_set_text(label_, vm.name);

        lv_obj_set_style_text_font(unit_, type.unit, 0);
        lv_label_set_text(unit_, vm.unit_label);

        lv_obj_set_hidden(arc_, true);
        lv_obj_set_hidden(bar_, true);
        /* Gauge hides the unit outright rather than risk it — the ring's
         * size already only has room for the bare value (see ARC_SIZE_*
         * above). Digital/Bar have the horizontal room to spare. */
        lv_obj_set_hidden(
            unit_, vm.mode == DisplayMode::Gauge || vm.unit_label[0] == '\0');

        uint8_t    pct    = range_pct_to_100(vm.range_pct);
        lv_coord_t offset = content_center_offset(caption_font);

        switch (vm.mode)
        {
            case DisplayMode::Bar: {
                lv_coord_t bar_h = vm.hero ? BAR_HEIGHT_HERO : BAR_HEIGHT;
                lv_obj_set_size(bar_, LV_PCT(80), bar_h);
                lv_obj_set_hidden(bar_, false);
                lv_bar_set_value(bar_, pct, LV_ANIM_OFF);
                lv_obj_set_style_bg_color(
                    bar_, indicator_color(*theme_, vm), LV_PART_INDICATOR);

                /* The value sits above the bar, so centering the bar alone
                 * at `offset` would leave the (value + bar) pair sitting
                 * higher than intended — shift the bar down by half the
                 * pair's combined height so the pair as a whole lands at
                 * `offset`, matching Digital/Gauge's single-element look. */
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
                lv_obj_set_hidden(arc_, false);
                lv_arc_set_value(arc_, pct);
                lv_obj_set_style_arc_color(
                    arc_, indicator_color(*theme_, vm), LV_PART_INDICATOR);

                /* The value sits inside the ring rather than beside it —
                 * centering it directly on the arc keeps the "graphic +
                 * number together" rule from adding a third vertical
                 * element to position. */
                lv_obj_align_to(data_, arc_, LV_ALIGN_CENTER, 0, 0);
                break;
            }

            case DisplayMode::Digital:
            default:
                lv_obj_align(data_, LV_ALIGN_CENTER, 0, offset);
                lv_obj_align_to(unit_, data_, LV_ALIGN_OUT_RIGHT_TOP, 3, 2);
                break;
        }

        /* A warning shades the whole panel rather than just tinting the
         * active visual — a bare colored number/bar/arc is easy to miss at
         * a glance next to five other panels; the panel's entire card
         * flipping to blue/red isn't. Digital has no separate indicator to
         * color at all, so this is its only warning signal. */
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

    void PanelWidget::set_visible(bool visible)
    {
        lv_obj_set_hidden(btn_, !visible);
    }

} // namespace gui
