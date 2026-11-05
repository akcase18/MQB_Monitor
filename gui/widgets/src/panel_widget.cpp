#include "panel_widget.hpp"
#include "mem_placement.hpp"

#include <math.h>
#include <string.h>

namespace gui
{

    namespace
    {
        /**
         * Everything below runs only from resize_contents() (FLASHMEM,
         * like it: none of it is on the per-refresh path).
         *
         * Everything below is sized from the panel's own content box, not
         * from a tier: cells run from 144x128 (landscape 5-panel's top
         * row) to 462x302 (one panel), and a size that suits one tier's
         * smallest cell wastes most of its biggest.
         */

        /* The Gauge's fill stroke scales with the ring (diameter / 14)
         * between these, so a big ring doesn't look spindly. */
        constexpr lv_coord_t ARC_WIDTH_MIN     = 7;
        constexpr lv_coord_t ARC_WIDTH_MAX     = 16;
        constexpr lv_coord_t ARC_WIDTH_DIVISOR = 14;

        /* Both graphics draw their track wider than their fill and push the
         * fill in by the difference, leaving a strip of track color ringing
         * the fill. That strip is the only thing separating an accent-
         * colored graphic from the panel behind it when the two are the
         * same hue — which is exactly what a warning does, since a warning
         * paints the whole panel blue/red (see set_view_model) and the
         * accent is itself user-selectable to blue or red. */
        constexpr lv_coord_t INDICATOR_INSET = 2;

        /**
         * The 270-degree ring's drawn height, as a percentage of its
         * diameter: the top half plus down to sin(45°) of the radius where
         * the ends sit, plus the ends' round caps. The rest of its box,
         * under the gap at the bottom, may overlap the caption.
         */
        constexpr lv_coord_t ARC_DRAWN_HEIGHT_PCT = 88;

        /* The ring shrinks to this much of the room it could fill, so it
         * doesn't run into the panel's edges -- but only as far as its
         * value still fits inside (see gauge_diameter()). */
        constexpr lv_coord_t ARC_FILL_PCT = 92;

        /* Clear space between a Gauge's value and the inside of its ring,
         * each side. */
        constexpr lv_coord_t GAUGE_TEXT_MARGIN = 4;

        /* The Bar's height is a tenth of the room above the caption,
         * between these. */
        constexpr lv_coord_t BAR_HEIGHT_MIN     = 18;
        constexpr lv_coord_t BAR_HEIGHT_MAX     = 32;
        constexpr lv_coord_t BAR_HEIGHT_DIVISOR = 10;
        constexpr lv_coord_t BAR_WIDTH_PCT      = 85;
        /* Between the value's digits and the bar, at least. */
        constexpr lv_coord_t BAR_DATA_GAP_MIN = 8;

        /* Clear space either side of words (an Enum's label). A number's
         * worst case is rarely shown, so it may use the full width; words
         * at full width would always touch the edges. */
        constexpr lv_coord_t TEXT_SIDE_MARGIN = 6;

        /* Between the value and its unit tag. */
        constexpr lv_coord_t UNIT_GAP = 3;

        /* The caption sits LABEL_BOTTOM_PAD above the panel's bottom
         * edge; the value and graphic share what's left above it, less
         * CONTENT_LABEL_GAP. The caption's position is the one thing that
         * never depends on mode or value. */
        constexpr lv_coord_t LABEL_BOTTOM_PAD  = 4;
        constexpr lv_coord_t CONTENT_LABEL_GAP = 4;

        /* Panel content size from which the caption steps up a size. */
        constexpr lv_coord_t CAPTION_LG_MIN_SIDE = 200;
        constexpr lv_coord_t CAPTION_XL_MIN_W    = 280;
        constexpr lv_coord_t CAPTION_XL_MIN_H    = 200;

        /**
         * The widest number a panel has to hold: a 5-glyph value with a
         * 3-glyph unit ("-14.5" psi, "-1.00" bar) or a 4-glyph value with
         * the 5-glyph "lb-ft" (torque has no decimals). A Gauge hides the
         * unit, so its worst case is just the 5 glyphs.
         */
        constexpr lv_coord_t WORST_VALUE_GLYPHS     = 5;
        constexpr lv_coord_t WORST_UNIT_GLYPHS      = 3;
        constexpr lv_coord_t WORST_LONG_UNIT_GLYPHS = 5;

        /* A unit font's line is at most this fraction of the value's. */
        constexpr lv_coord_t UNIT_LINE_DIVISOR = 4;

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

        FLASHMEM lv_coord_t clamp_coord(lv_coord_t v, lv_coord_t lo, lv_coord_t hi)
        {
            return v < lo ? lo : (v > hi ? hi : v);
        }

        /* Every value/caption font is monospace, so a glyph's advance
         * times the length is a string's exact width. */
        FLASHMEM lv_coord_t advance(const lv_font_t *font)
        {
            return lv_font_get_glyph_width(font, '0', 0);
        }

        /**
         * Where a font's ink sits within its line: `top` from the line's
         * top to the top of `c`, `height` of `c`. The line is taller than
         * the digits (room for descenders and accents), so centering the
         * line would leave the digits sitting high.
         */
        struct Ink
        {
                lv_coord_t top;
                lv_coord_t height;
        };

        FLASHMEM Ink ink_of(const lv_font_t *font, uint32_t c)
        {
            lv_font_glyph_dsc_t dsc;
            if (!lv_font_get_glyph_dsc(font, &dsc, c, 0))
            {
                return { 0, lv_font_get_line_height(font) };
            }
            lv_coord_t baseline = lv_font_get_line_height(font) - font->base_line;
            return { static_cast<lv_coord_t>(baseline - dsc.ofs_y - dsc.box_h),
                     static_cast<lv_coord_t>(dsc.box_h) };
        }

        FLASHMEM lv_coord_t worst_readout_width(const lv_font_t *value_font,
                                                const lv_font_t *unit_font)
        {
            lv_coord_t v          = advance(value_font);
            lv_coord_t u          = advance(unit_font);
            lv_coord_t short_unit = WORST_VALUE_GLYPHS * v + UNIT_GAP
                                    + WORST_UNIT_GLYPHS * u;
            lv_coord_t long_unit = (WORST_VALUE_GLYPHS - 1) * v + UNIT_GAP
                                   + WORST_LONG_UNIT_GLYPHS * u;
            return short_unit > long_unit ? short_unit : long_unit;
        }

        /**
         * The unit tag's font for a value in `value_font`: the biggest
         * caption-family font no more than a quarter of the value's line,
         * so it stays a tag beside a 128 px number instead of an 11 px
         * speck -- but a smaller one if that's what lets the worst-case
         * readout fit `width`, since the value matters more.
         */
        FLASHMEM const lv_font_t *unit_font(const Typography &type,
                                            const lv_font_t  *value_font,
                                            lv_coord_t        width)
        {
            lv_coord_t limit
                = lv_font_get_line_height(value_font) / UNIT_LINE_DIVISOR;
            const lv_font_t *const fonts[]
                = { type.label_xl, type.label_lg, type.label_sm };
            for (const lv_font_t *font : fonts)
            {
                if (lv_font_get_line_height(font) <= limit
                    && worst_readout_width(value_font, font) <= width)
                {
                    return font;
                }
            }
            return type.unit;
        }

        /**
         * The panel's room for the value and its graphic: the content box,
         * less the caption under it.
         */
        struct Room
        {
                lv_coord_t w;
                lv_coord_t h;
        };

        FLASHMEM lv_coord_t bar_height(const Room &room)
        {
            return clamp_coord(
                room.h / BAR_HEIGHT_DIVISOR, BAR_HEIGHT_MIN, BAR_HEIGHT_MAX);
        }

        FLASHMEM lv_coord_t bar_gap(const Ink &ink)
        {
            lv_coord_t gap = ink.height / 4;
            return gap > BAR_DATA_GAP_MIN ? gap : BAR_DATA_GAP_MIN;
        }

        FLASHMEM lv_coord_t arc_diameter(const Room &room)
        {
            lv_coord_t by_h = room.h * 100 / ARC_DRAWN_HEIGHT_PCT;
            return by_h < room.w ? by_h : room.w;
        }

        /**
         * Where every mode centers its value's digits, from the room's
         * top: the center of the biggest ring this room holds, with the
         * ring's drawn part centered in the room. That sits a little below
         * the room's middle -- the ring's empty bottom gap reaches down
         * toward the caption -- which reads as centered in the panel as a
         * whole rather than in the space above the caption. Digital and
         * Bar use it too, so a value doesn't jump when the mode changes.
         */
        FLASHMEM lv_coord_t value_center(const Room &room)
        {
            lv_coord_t d = arc_diameter(room);
            return (room.h - d * ARC_DRAWN_HEIGHT_PCT / 100) / 2 + d / 2;
        }

        /* `top`, moved as little as it takes to keep [top, top + h) inside
         * the room (the top edge winning if it can't). */
        FLASHMEM lv_coord_t keep_in_room(lv_coord_t  top,
                                         lv_coord_t  h,
                                         const Room &room)
        {
            if (top + h > room.h)
            {
                top = room.h - h;
            }
            return top > 0 ? top : 0;
        }

        FLASHMEM lv_coord_t arc_width(lv_coord_t diameter)
        {
            return clamp_coord(
                diameter / ARC_WIDTH_DIVISOR, ARC_WIDTH_MIN, ARC_WIDTH_MAX);
        }

        FLASHMEM lv_coord_t arc_track_width(lv_coord_t diameter)
        {
            return arc_width(diameter) + 2 * INDICATOR_INSET;
        }

        /**
         * Whether `glyphs` characters of `font` fit inside a ring of
         * `diameter`, centered: the chord across the ring's inside at the
         * ink's top and bottom edges has to hold them.
         */
        FLASHMEM bool fits_ring(const lv_font_t *font,
                       lv_coord_t       glyphs,
                       lv_coord_t       diameter)
        {
            float inner = diameter / 2.0f - arc_track_width(diameter)
                          - GAUGE_TEXT_MARGIN;
            float half_h = ink_of(font, '0').height / 2.0f;
            if (inner <= half_h)
            {
                return false;
            }
            float chord = 2.0f * sqrtf(inner * inner - half_h * half_h);
            return static_cast<float>(glyphs * advance(font)) <= chord;
        }

        /**
         * The ring's drawn diameter for a value in `font` that's `glyphs`
         * wide: ARC_FILL_PCT of the most the room allows, or less of a cut
         * if the value wouldn't fit inside that. The font was picked
         * against the full size, so that always fits.
         */
        FLASHMEM lv_coord_t gauge_diameter(const lv_font_t *font,
                                  lv_coord_t       glyphs,
                                  lv_coord_t       max_d)
        {
            lv_coord_t d   = max_d;
            lv_coord_t min = max_d * ARC_FILL_PCT / 100;
            while (d > min && fits_ring(font, glyphs, d - 1))
            {
                d--;
            }
            return d;
        }

        /**
         * Whether a value in `font` fits the room in `mode`: `glyphs`
         * wide (or the worst-case number with its unit when `glyphs` is
         * 0), and leaving room under it for a Bar.
         */
        FLASHMEM bool fits(const Typography &type,
                  const lv_font_t  *font,
                  lv_coord_t        glyphs,
                  DisplayMode       mode,
                  const Room       &room)
        {
            if (mode == DisplayMode::Gauge)
            {
                return fits_ring(font,
                                 glyphs > 0 ? glyphs : WORST_VALUE_GLYPHS,
                                 arc_diameter(room));
            }
            lv_coord_t w = glyphs > 0
                               ? glyphs * advance(font) + 2 * TEXT_SIDE_MARGIN
                               : worst_readout_width(
                                   font, unit_font(type, font, room.w));
            lv_coord_t h = lv_font_get_line_height(font);
            if (mode == DisplayMode::Bar)
            {
                Ink ink = ink_of(font, '0');
                h = ink.top + ink.height + bar_gap(ink) + bar_height(room);
            }
            return w <= room.w && h <= room.h;
        }

        /**
         * A number's font: the biggest of the value ladder that holds
         * the worst-case readout here, so every number this panel can
         * show stays the same size. data_text, smaller than the whole
         * ladder, is the last resort.
         */
        FLASHMEM const lv_font_t *number_font(const Typography &type,
                                     DisplayMode       mode,
                                     const Room       &room)
        {
            for (uint8_t i = type.data_count; i > 0; i--)
            {
                if (fits(type, type.data[i - 1], 0, mode, room))
                {
                    return type.data[i - 1];
                }
            }
            return type.data_text;
        }

        /**
         * Whether `font` has a glyph for every character of `text`. The
         * value fonts carry digits and only the characters the active
         * Enum labels use (tools/gen_value_fonts.py), so a label that
         * needs another one has to drop to a font that has it.
         */
        FLASHMEM bool font_has_glyphs(const lv_font_t *font, const char *text)
        {
            for (const char *c = text; *c != '\0'; c++)
            {
                lv_font_glyph_dsc_t dsc;
                if (!lv_font_get_glyph_dsc(
                        font, &dsc, static_cast<uint8_t>(*c), 0))
                {
                    return false;
                }
            }
            return true;
        }

        /**
         * Words (an Enum's label) get the size a number would in this
         * panel, or the biggest smaller one that has all their glyphs and
         * holds them on one line. The full-ASCII text fonts are the last
         * resort.
         */
        FLASHMEM const lv_font_t *text_font(const Typography     &type,
                                   const PanelViewModel &vm,
                                   const Room           &room)
        {
            lv_coord_t limit_h
                = lv_font_get_line_height(number_font(type, vm.mode, room));
            lv_coord_t n = static_cast<lv_coord_t>(strlen(vm.value_str));
            if (n == 0)
            {
                n = 1;
            }
            for (uint8_t i = type.data_count; i > 0; i--)
            {
                const lv_font_t *font = type.data[i - 1];
                if (lv_font_get_line_height(font) <= limit_h
                    && fits(type, font, n, vm.mode, room)
                    && font_has_glyphs(font, vm.value_str))
                {
                    return font;
                }
            }
            const lv_font_t *const rest[]
                = { type.data_text, type.label_lg, type.label_sm };
            for (const lv_font_t *font : rest)
            {
                if (fits(type, font, n, vm.mode, room)
                    && font_has_glyphs(font, vm.value_str))
                {
                    return font;
                }
            }
            return type.label_sm;
        }

        /**
         * The caption's font: a size up in a big panel, but never so big
         * the name wraps -- a smaller size on one line reads better than
         * a bigger one on two. Only label_sm is left to wrap.
         */
        FLASHMEM const lv_font_t *caption_font(const Typography &type,
                                      const char       *name,
                                      lv_coord_t        content_w,
                                      lv_coord_t        content_h)
        {
            lv_coord_t n = static_cast<lv_coord_t>(strlen(name));
            if (content_w >= CAPTION_XL_MIN_W && content_h >= CAPTION_XL_MIN_H
                && n * advance(type.label_xl) <= content_w)
            {
                return type.label_xl;
            }
            if ((content_w >= CAPTION_LG_MIN_SIDE
                 || content_h >= CAPTION_LG_MIN_SIDE)
                && n * advance(type.label_lg) <= content_w)
            {
                return type.label_lg;
            }
            return type.label_sm;
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
         * set_view_model(), so the label's position never depends on the
         * graphic/value's height the way it would flowing through a flex
         * column. */
        arc_ = lv_arc_create(btn_);
        lv_arc_set_bg_angles(arc_, ARC_BG_START_ANGLE, ARC_BG_END_ANGLE);
        lv_arc_set_range(arc_, 0, 100);
        lv_obj_remove_style(arc_, nullptr, LV_PART_KNOB | LV_STATE_ANY);
        lv_obj_set_clickable(arc_, false);
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
         * Gauge, whose value already has to fit inside the ring. Its font
         * scales with the value's (see unit_font()). */
        unit_ = lv_label_create(btn_);
        lv_obj_set_style_text_color(unit_, theme.palette.fg, 0);
        lv_obj_set_style_bg_opa(unit_, LV_OPA_TRANSP, 0);

        bar_ = lv_bar_create(btn_);
        lv_bar_set_range(bar_, 0, 100);
        lv_obj_set_style_bg_color(bar_, TRACK_COLOR, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(bar_, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_pad_all(bar_, INDICATOR_INSET, LV_PART_MAIN);
        lv_obj_set_style_bg_color(
            bar_, theme.palette.accent, LV_PART_INDICATOR);
        lv_obj_set_hidden(bar_, true);

        /* Fixed to the bottom of the panel regardless of mode or size — the
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
        /* Everything below is gated on what actually moved since the last
         * call. This runs for every visible panel several times a second,
         * and LVGL's setters do no such check of their own -- each one
         * invalidates the style cache and marks the panel dirty for
         * redraw even when handed the value it already holds. In the
         * steady state only value_str changes, so the common path is one
         * label update and a horizontal re-centering, instead of the
         * whole rebuild. */
        const bool first        = !applied_valid_;
        const bool mode_changed = first || vm.mode != applied_.mode;
        const bool kind_changed = first || vm.is_text != applied_.is_text;
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

        if (value_changed)
        {
            lv_label_set_text(data_, vm.value_str);
        }
        if (unit_changed)
        {
            lv_label_set_text(unit_, vm.unit_label);
        }
        if (mode_changed || unit_changed)
        {
            /* A Gauge's value has to fit inside the ring, which only has
             * room for the bare number. */
            lv_obj_set_hidden(unit_,
                              vm.mode == DisplayMode::Gauge
                                  || vm.unit_label[0] == '\0');
        }

        /**
         * The sizes depend on the cell, the mode and the caption (whose
         * height comes out of the room above it) -- and for words, on
         * their length. A move to a new cell always lands here:
         * set_visible() clears applied_valid_.
         */
        const bool resize = mode_changed || kind_changed || name_changed
                            || (vm.is_text && value_changed);
        if (resize)
        {
            resize_contents(vm, name_changed);
        }

        if (resize || value_changed || unit_changed)
        {
            place_value();
        }

        if (pct_changed || mode_changed)
        {
            uint8_t pct = range_pct_to_100(vm.range_pct);
            if (vm.mode == DisplayMode::Bar)
            {
                lv_bar_set_value(bar_, pct, LV_ANIM_OFF);
            }
            else if (vm.mode == DisplayMode::Gauge)
            {
                lv_arc_set_value(arc_, pct);
            }

            /* indicator_color() varies only with whether range_pct has
             * pegged outside [0,1]. */
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

    FLASHMEM __attribute__((noinline)) void
    PanelWidget::resize_contents(const PanelViewModel &vm, bool name_changed)
    {
        const Typography &type = theme_->typography;

        /* The grid sizes btn_ lazily -- resolve it so the sizes below
         * are this cell's, not a stale one's. */
        lv_obj_update_layout(btn_);
        lv_coord_t content_w = lv_obj_get_content_width(btn_);
        lv_coord_t content_h = lv_obj_get_content_height(btn_);

        lv_obj_set_style_text_font(
            label_,
            caption_font(type, vm.name, content_w, content_h),
            0);
        if (name_changed)
        {
            lv_label_set_text(label_, vm.name);
        }
        lv_obj_update_layout(label_);

        Room room { content_w,
                    static_cast<lv_coord_t>(
                        content_h - LABEL_BOTTOM_PAD
                        - lv_obj_get_height(label_) - CONTENT_LABEL_GAP) };

        const lv_font_t *value_font
            = vm.is_text ? text_font(type, vm, room)
                         : number_font(type, vm.mode, room);
        const lv_font_t *tag_font = unit_font(type, value_font, room.w);
        lv_obj_set_style_text_font(data_, value_font, 0);
        lv_obj_set_style_text_font(unit_, tag_font, 0);

        Ink ink  = ink_of(value_font, '0');
        unit_dy_ = static_cast<lv_coord_t>(ink.top - ink_of(tag_font, 'l').top);

        lv_obj_set_hidden(arc_, vm.mode != DisplayMode::Gauge);
        lv_obj_set_hidden(bar_, vm.mode != DisplayMode::Bar);

        /* The digits' top if they're centered on the shared line; each
         * mode keeps them (and its graphic) inside the room. */
        const lv_coord_t center = value_center(room);
        const lv_coord_t digits_top
            = static_cast<lv_coord_t>(center - ink.height / 2);

        switch (vm.mode)
        {
            case DisplayMode::Bar: {
                /* The digits on the line and the bar under them, the two
                 * moved up together if that leaves the bar less clear of
                 * the caption than it is of the digits -- a bar resting
                 * on the caption reads as underlining it. Where even that
                 * doesn't fit, the two are centered as a group. */
                lv_coord_t bar_h = bar_height(room);
                lv_coord_t gap   = bar_gap(ink);
                lv_coord_t group = static_cast<lv_coord_t>(
                    ink.height + gap + bar_h);
                lv_coord_t top
                    = group + gap > room.h
                          ? static_cast<lv_coord_t>((room.h - group) / 2)
                          : keep_in_room(digits_top,
                                         static_cast<lv_coord_t>(group + gap),
                                         room);
                lv_obj_set_size(
                    bar_, content_w * BAR_WIDTH_PCT / 100, bar_h);
                lv_obj_align(bar_,
                             LV_ALIGN_TOP_MID,
                             0,
                             top + ink.height + gap);
                data_y_ = static_cast<lv_coord_t>(top - ink.top);
                break;
            }

            case DisplayMode::Gauge: {
                lv_coord_t glyphs
                    = vm.is_text
                          ? static_cast<lv_coord_t>(strlen(vm.value_str))
                          : WORST_VALUE_GLYPHS;
                lv_coord_t d = gauge_diameter(
                    value_font, glyphs, arc_diameter(room));
                lv_obj_set_size(arc_, d, d);
                lv_obj_set_style_arc_width(
                    arc_, arc_track_width(d), LV_PART_MAIN);
                lv_obj_set_style_arc_width(
                    arc_, arc_width(d), LV_PART_INDICATOR);
                /* The ring centered on the line, and the digits in it.
                 * A ring cut below the biggest is off its drawn-part
                 * center by at most a pixel or two. */
                lv_obj_align(arc_, LV_ALIGN_TOP_MID, 0, center - d / 2);
                data_y_ = static_cast<lv_coord_t>(digits_top - ink.top);
                break;
            }

            case DisplayMode::Digital:
            default:
                data_y_ = static_cast<lv_coord_t>(
                    keep_in_room(digits_top, ink.height, room) - ink.top);
                break;
        }
    }

    void PanelWidget::place_value()
    {
        /**
         * The value centered with its unit tag hanging off the right,
         * slid left just enough to keep the tag inside the panel (never
         * past the left edge). The worst-case readout always fits, so
         * this only moves a value when the number is that wide.
         */
        lv_obj_update_layout(data_);
        lv_coord_t content_w = lv_obj_get_content_width(btn_);
        lv_coord_t data_w    = lv_obj_get_width(data_);
        lv_coord_t x         = (content_w - data_w) / 2;
        bool       has_unit  = !lv_obj_has_flag(unit_, LV_OBJ_FLAG_HIDDEN);
        if (has_unit)
        {
            lv_coord_t overflow
                = x + data_w + UNIT_GAP + lv_obj_get_width(unit_) - content_w;
            if (overflow > 0)
            {
                x -= overflow < x ? overflow : x;
            }
        }
        lv_obj_set_pos(data_, x, data_y_);
        if (has_unit)
        {
            lv_obj_set_pos(unit_, x + data_w + UNIT_GAP, data_y_ + unit_dy_);
        }
    }

    void PanelWidget::set_visible(bool visible)
    {
        lv_obj_set_hidden(btn_, !visible);

        /* This is only ever called from InfoScreen::init()/rebuild(), i.e.
         * exactly when the panel is about to be given a new grid cell (and
         * therefore a new size). Every size and position set_view_model()
         * works out comes from the cell's size, so drop the cached
         * view-model to force the next refresh to redo them all. */
        applied_valid_ = false;
    }

} // namespace gui
