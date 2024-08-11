#include "Configurator.hpp"

/****************
 * Constructors *
 ****************/

Configurator::Configurator()
{

}

/***************************
 * Public Helper Functions *
 ***************************/

void Configurator::show_config_window()
{
    window = lv_obj_create(lv_scr_act());
}

/****************************
 * Private Helper Functions *
 ****************************/

void Configurator::config_style_init()
{
    /* Set the style of the configuration window */
    lv_style_init(style_window);
    lv_style_set_bg_color(style_window, lv_palette_darken(LV_PALETTE_GREY, 4));

    /* Set the style of the "done" button */
    lv_style_init(style_btn_done);
    lv_style_set_radius(style_btn_done, 5);
    lv_style_set_opa(style_btn_done, LV_OPA_100);
    lv_style_set_bg_color(style_btn_done, lv_palette_darken(LV_PALETTE_GREY, 4));
    lv_style_set_text_color(style_btn_done, lv_palette_lighten(LV_PALETTE_GREY, 4));
    lv_style_set_text_font(style_btn_done, done_label_font);
    lv_style_set_text_letter_space(style_btn_done, 0);
    lv_style_set_shadow_width(style_btn_done, 0);

    /* Set the style of the radio button for selecting unit region */
    lv_style_init(style_radio_unchecked);
    lv_style_set_radius(style_radio_unchecked, LV_RADIUS_CIRCLE);
    lv_style_set_text_color(style_radio_unchecked, lv_palette_lighten(LV_PALETTE_GREY, 5));
    /* Set the style of the radio button when checked */
    lv_style_init(style_radio_checked);
    lv_style_set_bg_img_src(style_radio_checked, NULL);

    /* Set the style for the flex layout objects */
    lv_style_init(style_layout);
    lv_style_set_bg_color(style_layout, lv_palette_darken(LV_PALETTE_GREY, 4));
    lv_style_set_border_width(style_layout, 0);
    lv_style_set_text_color(style_layout, lv_palette_lighten(LV_PALETTE_GREY, 5));
    lv_style_set_text_font(style_layout, drop_down_font);
}

void Configurator::setup_config_window()
{
    lv_obj_set_size(window, 480, 320); // Set to the proper window size
    lv_obj_center(window); // Puts window in the center of the display
    lv_obj_add_style(window, style_window, 0); // Add the style to the window
    /* Turn off scrolling */
    lv_obj_set_scrollbar_mode(window, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(window, LV_OBJ_FLAG_SCROLLABLE);
    /* Add elements to window */
}