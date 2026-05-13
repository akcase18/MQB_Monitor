#pragma once

#include <lvgl.h>

/**
 * Holds (and initalizes) all styles used
 */

/*==============================================================================
 Constants
 =============================================================================*/

constexpr uint16_t LCD_X = 480;
constexpr uint16_t LCD_Y = 320;

/*==============================================================================
 Fonts
 =============================================================================*/

LV_FONT_DECLARE(digital_display_80_v2);
LV_FONT_DECLARE(digital_display_115_v2);
LV_FONT_DECLARE(vw_16);
LV_FONT_DECLARE(vw_18);
LV_FONT_DECLARE(vw_24);

/*==============================================================================
 Screen Styles
 =============================================================================*/

static const lv_style_const_prop_t style_screen_props[] = {
    LV_STYLE_CONST_PAD_COLUMN(0),
    LV_STYLE_CONST_PAD_ROW(0),
    LV_STYLE_CONST_BG_COLOR(lv_color_black()),
    LV_STYLE_CONST_PROPS_END,
};
LV_STYLE_CONST_INIT(style_screen, style_screen_props);

/*==============================================================================
 Panel Styles
 =============================================================================*/

/**
 * Button
 *
 * This needs to contain only the things that belong to the
 * button specifically. No text sizes, no alignment of child
 * objects, etc. Those belong to the data and labels.
 *
 * Maybe this should hold width and height values as well?
 * If that's the case:
 * Small Button:
 * - 1/3 the width of the screen (160px)
 * - 40-45% the height of the screen (128px-144px, old was 140px)
 *
 * Large Button:
 * - 1/2 the width of the screen (240px)
 * - 60-55% the height of the screen (192px-176px, old was 180px)
 */
static const lv_style_const_prop_t style_panel_btn_props[] = {
    LV_STYLE_CONST_FLEX_GROW(1),
    LV_STYLE_CONST_BG_COLOR(lv_color_black()),
    LV_STYLE_CONST_TEXT_COLOR(lv_color_white()),
    LV_STYLE_CONST_PAD_BOTTOM(2),
    // LV_STYLE_CONST_BORDER_WIDTH(1),
    LV_STYLE_CONST_RADIUS(10),
    LV_STYLE_CONST_SHADOW_WIDTH(0),
    LV_STYLE_CONST_PROPS_END,
};
LV_STYLE_CONST_INIT(style_panel_btn, style_panel_btn_props);

static const lv_style_const_prop_t style_panel_btn_sm_props[] = {
    LV_STYLE_CONST_HEIGHT((int32_t)(320 * 0.45)),
    LV_STYLE_CONST_PROPS_END,
};
LV_STYLE_CONST_INIT(style_panel_btn_sm, style_panel_btn_sm_props);

static const lv_style_const_prop_t style_panel_btn_lg_props[] = {
    LV_STYLE_CONST_HEIGHT((int32_t)(320 * 0.55)),
    LV_STYLE_CONST_PROPS_END,
};
LV_STYLE_CONST_INIT(style_panel_btn_lg, style_panel_btn_lg_props);

/**
 * Button - Pressed
 *
 * This will lighten the button a little bit when it is pressed.
 */
static const lv_style_const_prop_t style_panel_btn_pressed_props[] = {
    LV_STYLE_CONST_BG_COLOR(lv_palette_darken(LV_PALETTE_GREY, 3)),
    LV_STYLE_CONST_PROPS_END,
};
LV_STYLE_CONST_INIT(style_panel_btn_pressed, style_panel_btn_pressed_props);

/**
 * Data - Base
 *
 * Base style for the data label in the middle of the button,
 *
 */
static const lv_style_const_prop_t style_panel_data_props[] = {
    LV_STYLE_CONST_ALIGN(LV_ALIGN_CENTER),
    LV_STYLE_CONST_TEXT_COLOR(lv_color_white()),
    LV_STYLE_CONST_PROPS_END,
};
LV_STYLE_CONST_INIT(style_panel_data, style_panel_data_props);

/**
 * Data - Small
 *
 * Builds off of base data style. Uses smaller fonts.
 */
static const lv_style_const_prop_t style_panel_data_sm_props[] = {
    LV_STYLE_CONST_TEXT_FONT(&digital_display_80_v2),
    LV_STYLE_CONST_TRANSLATE_Y(-10),
    LV_STYLE_CONST_PROPS_END,
};
LV_STYLE_CONST_INIT(style_panel_data_sm, style_panel_data_sm_props);

/**
 * Data - Large
 *
 * Builds off of base data style. Uses larger fonts.
 */
static const lv_style_const_prop_t style_panel_data_lg_props[] = {
    LV_STYLE_CONST_TEXT_FONT(&digital_display_115_v2),
    LV_STYLE_CONST_TRANSLATE_Y(-10),
    LV_STYLE_CONST_PROPS_END,
};
LV_STYLE_CONST_INIT(style_panel_data_lg, style_panel_data_lg_props);

/**
 * Label - Base
 *
 *
 */
static const lv_style_const_prop_t style_panel_label_props[] = {
    LV_STYLE_CONST_ALIGN(LV_ALIGN_BOTTOM_MID),
    LV_STYLE_CONST_WIDTH(LV_SIZE_CONTENT),
    // LV_STYLE_CONST_BORDER_COLOR(lv_color_white()),
    // LV_STYLE_CONST_BORDER_WIDTH(1),
    LV_STYLE_CONST_PROPS_END,
};
LV_STYLE_CONST_INIT(style_panel_label, style_panel_label_props);

/**
 * Label - Small
 *
 * Builds off of base label style. Uses smaller fonts.
 */
static const lv_style_const_prop_t style_panel_label_sm_props[] = {
    LV_STYLE_CONST_TEXT_FONT(&vw_16),
    LV_STYLE_CONST_HEIGHT(18),
    LV_STYLE_CONST_PROPS_END,
};
LV_STYLE_CONST_INIT(style_panel_label_sm, style_panel_label_sm_props);

/**
 * Label - Large
 *
 * Builds off of base label style. Uses larger fonts.
 */
static const lv_style_const_prop_t style_panel_label_lg_props[] = {
    LV_STYLE_CONST_TEXT_FONT(&vw_18),
    LV_STYLE_CONST_HEIGHT(20),
    LV_STYLE_CONST_PROPS_END,
};
LV_STYLE_CONST_INIT(style_panel_label_lg, style_panel_label_lg_props);


/**
 * Information Roller
 *
 * Used to select Information objects in the configuration screen.
 */
static const lv_style_const_prop_t style_config_roller_props[] = {
    LV_STYLE_CONST_BG_COLOR(lv_color_white()),
    LV_STYLE_CONST_TEXT_COLOR(lv_color_black()),
    LV_STYLE_CONST_ALIGN(LV_ALIGN_TOP_LEFT),
    LV_STYLE_CONST_TEXT_FONT(&vw_18),
    LV_STYLE_CONST_WIDTH(320),
    LV_STYLE_CONST_PROPS_END,
};
LV_STYLE_CONST_INIT(style_config_roller, style_config_roller_props);

/**
 * Radio Button (Unselected)
 */
static const lv_style_const_prop_t style_config_radio_btn_props[] = {
    LV_STYLE_CONST_BG_COLOR(lv_color_black()),
    LV_STYLE_CONST_TEXT_COLOR(lv_color_white()),
    LV_STYLE_CONST_TEXT_FONT(&vw_18),
    LV_STYLE_CONST_PAD_LEFT(20),
    LV_STYLE_CONST_PAD_RIGHT(20),
    LV_STYLE_CONST_WIDTH(160),
    LV_STYLE_CONST_PROPS_END,
};
LV_STYLE_CONST_INIT(style_config_radio_btn, style_config_radio_btn_props);

/**
 * Radio Button (Selected)
 */
static const lv_style_const_prop_t style_config_radio_btn_sel_props[] = {
    LV_STYLE_CONST_BG_IMAGE_SRC(NULL),
    LV_STYLE_CONST_PROPS_END,
};
LV_STYLE_CONST_INIT(style_config_radio_btn_sel,
                    style_config_radio_btn_sel_props);

/**
 * Done button
 *
 * Used to go back (and save your changes) after opening a configuration or
 * settings screen.
 */
static const lv_style_const_prop_t style_config_done_props[] = {
    LV_STYLE_CONST_BG_COLOR(lv_color_black()),
    LV_STYLE_CONST_TEXT_COLOR(lv_color_white()),
    LV_STYLE_CONST_RADIUS(0),
    LV_STYLE_CONST_TEXT_FONT(&lv_font_montserrat_32),
    LV_STYLE_CONST_ALIGN(LV_ALIGN_BOTTOM_RIGHT),
    LV_STYLE_CONST_PAD_BOTTOM(20),
    LV_STYLE_CONST_PAD_RIGHT(20),
    LV_STYLE_CONST_PROPS_END,
};
LV_STYLE_CONST_INIT(style_config_done, style_config_done_props);

/**
 * Settings button for the Configuration Screen
 *
 * Used to open up additional settings.
 */
static const lv_style_const_prop_t style_config_settings_props[] = {
    LV_STYLE_CONST_BG_COLOR(lv_color_black()),
    LV_STYLE_CONST_TEXT_COLOR(lv_color_white()),
    LV_STYLE_CONST_RADIUS(0),
    LV_STYLE_CONST_TEXT_FONT(&lv_font_montserrat_32),
    LV_STYLE_CONST_ALIGN(LV_ALIGN_BOTTOM_LEFT),
    LV_STYLE_CONST_PAD_BOTTOM(20),
    LV_STYLE_CONST_PAD_LEFT(20),
    LV_STYLE_CONST_PROPS_END,
};
LV_STYLE_CONST_INIT(style_config_settings, style_config_settings_props);