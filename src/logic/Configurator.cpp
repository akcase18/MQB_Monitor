#include "Configurator.hpp"

const lv_font_t *done_label_font = &vw_24;

/**********************************
 * Configurator Object Definition *
 **********************************/

DMAMEM Configurator config = Configurator();

/****************
 * Constructors *
 ****************/

Configurator::Configurator() {}

/***************************
 * Public Helper Functions *
 ***************************/

void Configurator::init_configurator()
{
    style_init();
    init_window();
    init_window_layout();
    init_drop_down();
    init_radio_btns();
    init_done_btn();
}

void Configurator::open_window()
{
    lv_screen_load(window);
}

void Configurator::close_window()
{
}

void Configurator::select_imperial_radio_btn()
{
    lv_obj_add_state(radio_imperial, LV_STATE_CHECKED);
    lv_obj_clear_state(radio_metric, LV_STATE_CHECKED);
}

void Configurator::select_metric_radio_btn()
{
    lv_obj_clear_state(radio_imperial, LV_STATE_CHECKED);
    lv_obj_add_state(radio_metric, LV_STATE_CHECKED);
}

void Configurator::set_callback(lv_obj_t *object, void (*function)(lv_event_t *e), lv_event_code_t state, void *user_data)
{
}

/****************************
 * Private Helper Functions *
 ****************************/

void Configurator::style_init()
{
    /* Set the style of the configurator window */
    lv_style_init(&style_window);
    lv_style_set_bg_color(&style_window, lv_palette_darken(LV_PALETTE_GREY, 4));

    /* Set the style of the done button */
    lv_style_init(&style_btn_done);
    lv_style_set_radius(&style_btn_done, 5);
    lv_style_set_bg_color(&style_btn_done, lv_palette_lighten(LV_PALETTE_GREY, 4));
    lv_style_set_text_color(&style_btn_done, lv_palette_darken(LV_PALETTE_GREY, 4));
    // lv_style_set_text_font(&style_btn_done, done_label_font);
    lv_style_set_text_letter_space(&style_btn_done, 0);
    lv_style_set_shadow_width(&style_btn_done, 0);

    /* Set the style of the label in the done button */
    lv_style_init(&style_label_done);
    lv_style_set_text_font(&style_label_done, done_label_font);

    /* Set the style of the radio buttons (for selecting unit region) */
    lv_style_init(&style_radio_unchecked);
    lv_style_set_radius(&style_radio_unchecked, LV_RADIUS_CIRCLE);
    lv_style_set_text_color(&style_radio_unchecked, lv_palette_lighten(LV_PALETTE_GREY, 5));

    /* Set the style of the radio buttons when selected */
    lv_style_init(&style_radio_checked);
    lv_style_set_bg_img_src(&style_radio_checked, NULL);

    /* Set the style for the flex layout objects */
    lv_style_init(&style_layout);
    lv_style_set_bg_color(&style_layout, lv_palette_darken(LV_PALETTE_GREY, 4));
    lv_style_set_border_width(&style_layout, 0);
    lv_style_set_text_color(&style_layout, lv_palette_lighten(LV_PALETTE_GREY, 5));
    lv_style_set_text_font(&style_layout, &vw_24);
}

void Configurator::init_window()
{
    window = lv_obj_create(NULL);
    lv_obj_set_size(window, LCD_HORZ, LCD_VERT);
    lv_obj_center(window);
    lv_obj_add_style(window, &style_window, 0);
    /* Turn off scrolling */
    lv_obj_set_scrollbar_mode(window, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(window, LV_OBJ_FLAG_SCROLLABLE);
}

void Configurator::init_window_layout()
{
    /* Add formatting to window */
    flex_col_1 = lv_obj_create(window);                    // Adds flex column to the Configurator window
    lv_obj_set_flex_flow(flex_col_1, LV_FLEX_FLOW_COLUMN); // Sets column to use flex flow layout
    lv_obj_set_size(flex_col_1, lv_pct(65), lv_pct(100));  // Set column to take up 65% of window width
    lv_obj_add_style(flex_col_1, &style_layout, 0);        // Add flex layout style to column
    flex_col_2 = lv_obj_create(window);                    // Adds flex clumn to the Configurator window
    lv_obj_set_flex_flow(flex_col_2, LV_FLEX_FLOW_COLUMN); // Sets column to use flex flow layout
    lv_obj_set_size(flex_col_2, lv_pct(35), lv_pct(100));  // Set column to take up 35% of window width
    lv_obj_set_x(flex_col_2, lv_pct(65));                  // Set column to take up 60% of window height
    lv_obj_add_style(flex_col_2, &style_layout, 0);        // Add flex layout style to column
}

void Configurator::init_drop_down()
{
    String drop_down_string;
    for (uint i = 0; i < info_list.size(); i++)
    {
        drop_down_string += info_list[i]->get_name() + "\n";
    }
    drop_down = lv_dropdown_create(flex_col_1);
    lv_dropdown_set_options_static(drop_down, drop_down_string.c_str());
    lv_obj_set_align(drop_down, LV_ALIGN_TOP_MID);
    lv_obj_set_size(drop_down, lv_pct(100), 50);
}

void Configurator::init_done_btn()
{
    int32_t btn_w = 120;
    int32_t btn_h = 65;
    btn_done = lv_obj_create(window);
    lv_obj_set_size(btn_done, btn_w, btn_h);
    lv_obj_set_pos(btn_done, LCD_HORZ - btn_w - 20, LCD_VERT - btn_h - 20);
    /* Turn off scrolling */
    lv_obj_set_scrollbar_mode(btn_done, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(btn_done, LV_OBJ_FLAG_SCROLLABLE);
    /* Add "Done" text */
    label_done = lv_label_create(btn_done);
    lv_obj_add_style(label_done, &style_label_done, 0);
    lv_label_set_text(label_done, "Done");
    lv_obj_set_align(label_done, LV_ALIGN_CENTER);
}

void Configurator::init_radio_btns()
{
    /* Imperial Radio Button */
    radio_imperial = lv_checkbox_create(flex_col_2);
    lv_checkbox_set_text(radio_imperial, "Imperial");
    lv_obj_add_flag(radio_imperial, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_style(radio_imperial, &style_radio_unchecked, LV_PART_INDICATOR);
    lv_obj_add_style(radio_imperial, &style_radio_checked, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_align(radio_imperial, LV_ALIGN_TOP_MID);
    /* Metric Radio Button */
    radio_metric = lv_checkbox_create(flex_col_2);
    lv_checkbox_set_text(radio_metric, "Metric");
    lv_obj_add_flag(radio_metric, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_style(radio_metric, &style_radio_unchecked, LV_PART_INDICATOR);
    lv_obj_add_style(radio_metric, &style_radio_checked, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_align(radio_metric, LV_ALIGN_TOP_MID, lv_pct(0), lv_pct(25));
}

Information *Configurator::get_info_from_drop_down()
{
}