#ifndef FONTS_H
#define FONTS_H

#include "lvgl.h"

/*********
 * Fonts *
 *********/

/* Data */
LV_FONT_DECLARE(digital_display_80);
LV_FONT_DECLARE(digital_display_90);
LV_FONT_DECLARE(digital_display_100);
LV_FONT_DECLARE(digital_display_115);
LV_FONT_DECLARE(digital_display_120);
LV_FONT_DECLARE(digital_display_130);
LV_FONT_DECLARE(digital_display_135);
LV_FONT_DECLARE(digital_display_140);
/* Labels */
LV_FONT_DECLARE(vw_14);
LV_FONT_DECLARE(vw_16);
LV_FONT_DECLARE(vw_18);
LV_FONT_DECLARE(vw_24);
/* Font Objects */
const lv_font_t *top_data_font = &digital_display_100; // Classic 7-segment display @ 100 pixels tall
const lv_font_t *btm_data_font = &digital_display_140; // Classic 7-segment display @ 140 pixels tall
const lv_font_t *top_label_font = &vw_16;              // VW font @ 16 pixels tall
const lv_font_t *btm_label_font = &vw_18;              // VW font @ 18 pixels tall
const lv_font_t *done_label_font = &vw_24;             // VW font @ 24 pixels tall
const lv_font_t *drop_down_font = &vw_24;              // VW font @ 24 pixels tall

#endif