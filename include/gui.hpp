// #include <Arduino.h>
// #include "can_logic.hpp"
#include "Configurator.hpp"

/***********
 * Defines *
 ***********/

#define LCD_HORZ 480
#define LCD_VERT 320

/***********************
 ***********************
 * NEW STUFF GOES HERE *
 ***********************
 ***********************/

/***********************
 ***********************
 * OLD STUFF GOES HERE *
 ***********************
 ***********************/

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
// LV_FONT_DECLARE(vw_24);
/* Font Objects */
const lv_font_t *top_data_font = &digital_display_100;
const lv_font_t *btm_data_font = &digital_display_140;
const lv_font_t *top_label_font = &vw_16;
const lv_font_t *btm_label_font = &vw_18;
const lv_font_t *drop_down_font = &vw_24;

/********
 * Grid *
 ********/

static lv_coord_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
static lv_coord_t row_dsc[] = {LV_GRID_FR(6), LV_GRID_FR(1), LV_GRID_FR(8), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};

/***********
 * Display *
 ***********/

static lv_display_t *display;
static lv_color_t buf_1[(LCD_HORZ * LCD_VERT) / 10];
static lv_color_t buf_2[(LCD_HORZ * LCD_VERT) / 10];

/*******************
 * Variable Status *
 *******************/

static bool imperial = true;      // Whether the unit radio button selected imperial or not

/**************************
 **************************
 * END BLOCK OF OLD STUFF *
 **************************
 **************************/

/********************
 * Static Functions *
 ********************/

/***** Initialization *****/

/**
 * Initialize all styles for objects and labels
 */
static void style_init();

/**
 * Initialize/set up the styles for all the Panels.
 */
static void panel_style_init();

/***** Transforms *****/

/**
 * Darken the button by the specified opacity
 */
static lv_color_t darken(const lv_color_filter_dsc_t *dsc, lv_color_t color, lv_opa_t opa);

/******************
 * Initialization *
 ******************/

static void style_init()
{
    /* Set the style of the top data buttons */
    lv_style_init(&style_btn_top);
    lv_style_set_radius(&style_btn_top, 2);
    lv_style_set_opa(&style_btn_top, LV_OPA_100);
    lv_style_set_bg_color(&style_btn_top, lv_palette_darken(LV_PALETTE_GREY, 4));
    lv_style_set_text_color(&style_btn_top, lv_palette_lighten(LV_PALETTE_GREY, 4));
    lv_style_set_text_font(&style_btn_top, top_data_font);
    lv_style_set_text_letter_space(&style_btn_top, 0);
    lv_style_set_shadow_width(&style_btn_top, 0);
    LV_UNUSED(style_btn_top);

    /* Set the style of the bottom data buttons */
    lv_style_init(&style_btn_btm);
    lv_style_set_radius(&style_btn_btm, 2);
    lv_style_set_opa(&style_btn_btm, LV_OPA_100);
    lv_style_set_bg_color(&style_btn_btm, lv_palette_darken(LV_PALETTE_GREY, 4));
    lv_style_set_text_color(&style_btn_btm, lv_palette_lighten(LV_PALETTE_GREY, 4));
    lv_style_set_text_font(&style_btn_btm, btm_data_font);
    lv_style_set_text_letter_space(&style_btn_btm, 0);
    lv_style_set_shadow_width(&style_btn_btm, 0);
    LV_UNUSED(style_btn_btm);

    /* Darken button when pressed */
    static lv_color_filter_dsc_t color_filter_btn;
    lv_color_filter_dsc_init(&color_filter_btn, darken);
    lv_style_init(&style_btn_pressed);
    lv_style_set_color_filter_dsc(&style_btn_pressed, &color_filter_btn);
    lv_style_set_color_filter_opa(&style_btn_pressed, LV_OPA_20);
    LV_UNUSED(style_btn_pressed);

    /* Set the style of the top row label object */
    lv_style_init(&style_label_top);
    lv_style_set_text_color(&style_label_top, lv_palette_lighten(LV_PALETTE_GREY, 5));
    lv_style_set_text_font(&style_label_top, top_label_font);
    lv_style_set_text_letter_space(&style_label_top, 0);
    LV_UNUSED(style_label_top);

    /* Set the style of the bottom row label object */
    lv_style_init(&style_label_btm);
    lv_style_set_text_color(&style_label_btm, lv_palette_lighten(LV_PALETTE_GREY, 5));
    lv_style_set_text_font(&style_label_btm, btm_label_font);
    lv_style_set_text_letter_space(&style_label_btm, 0);
    LV_UNUSED(style_label_btm);
    
    panel_style_init();
}

static void panel_style_init()
{
    /***** Top Left Panel *****/
    panel_tl.style_btn = &style_btn_top;
    panel_tl.style_btn_pressed = &style_btn_pressed;
    panel_tl.style_label = &style_label_top;
    /***** Top Center Panel *****/
    panel_tc.style_btn = &style_btn_top;
    panel_tc.style_btn_pressed = &style_btn_pressed;
    panel_tc.style_label = &style_label_top;
    /***** Top Right Panel *****/
    panel_tr.style_btn = &style_btn_top;
    panel_tr.style_btn_pressed = &style_btn_pressed;
    panel_tr.style_label = &style_label_top;
    /***** Bottom Left Panel *****/
    panel_bl.style_btn = &style_btn_btm;
    panel_bl.style_btn_pressed = &style_btn_pressed;
    panel_bl.style_label = &style_label_btm;
    /***** Bottom Right Panel *****/
    panel_br.style_btn = &style_btn_btm;
    panel_br.style_btn_pressed = &style_btn_pressed;
    panel_br.style_label = &style_label_btm;
}

/**************
 * Transforms *
 **************/

static lv_color_t darken(const lv_color_filter_dsc_t *dsc, lv_color_t color, lv_opa_t opa)
{
    LV_UNUSED(dsc);
    return lv_color_darken(color, opa);
}