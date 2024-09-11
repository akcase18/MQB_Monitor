#include "can_handler.hpp"

/********************
 * Global Constants *
 ********************/

/******************
 * Style Creation *
 ******************/

/***** Layout *****/
static lv_obj_t *layout_screen;
static lv_style_t *style_layout_screen;
static lv_coord_t col_layout[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
static lv_coord_t row_layout[] = {LV_GRID_FR(6), LV_GRID_FR(1), LV_GRID_FR(8), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
/* Button Styles */
lv_style_t style_btn_top;     // Top row button style
lv_style_t style_btn_btm;     // Bottom row button style
lv_style_t style_btn_pressed; // Button style when pressed
/* Label Styles */
lv_style_t style_label_top; // Top row label style
lv_style_t style_label_btm; // Bottom row label style

/******************
 * Display Buffer *
 ******************/

static lv_color_t disp_buf_1[(LCD_HORZ * LCD_VERT) / 10];
static lv_color_t disp_buf_2[(LCD_HORZ * LCD_VERT) / 10];

/********************
 * Static Functions *
 ********************/

/***** Initialization *****/

/**
 * Initialize all styles for objects and labels
 */
void style_init(void);

/***** Transforms *****/

/**
 * Darken the button by the specified opacity
 */
lv_color_t darken(const lv_color_filter_dsc_t *dsc, lv_color_t color, lv_opa_t opa);

/***** Layout *****/
void open_layout_screen();
void setup_layout_screen();

/***** Timer Callbacks *****/

void top_left_timer_cb(lv_timer_t *timer);
void top_center_timer_cb(lv_timer_t *timer);
void top_right_timer_cb(lv_timer_t *timer);
void bottom_left_timer_cb(lv_timer_t *timer);
void bottom_right_timer_cb(lv_timer_t *timer);

/**
 * Update the data in each button
 */
void update_buttons();

/******************
 * Initialization *
 ******************/

void style_init(void)
{
    /***** Layout *****/
    /* Style of the screen for layout */
    lv_style_init(style_layout_screen);
    lv_style_set_bg_color(style_layout_screen, lv_palette_darken(LV_PALETTE_GREY, 4));
    lv_style_set_border_width(style_layout_screen, 0);
    lv_style_set_radius(style_layout_screen, 0);

    /* Set the style of the top data buttons */
    lv_style_init(&style_btn_top);
    lv_style_set_radius(&style_btn_top, 2);
    lv_style_set_opa(&style_btn_top, LV_OPA_100);
    lv_style_set_bg_color(&style_btn_top, lv_palette_darken(LV_PALETTE_GREY, 4));
    lv_style_set_text_color(&style_btn_top, lv_palette_lighten(LV_PALETTE_GREY, 4));
    lv_style_set_text_font(&style_btn_top, &digital_display_100);
    lv_style_set_text_letter_space(&style_btn_top, 0);
    lv_style_set_shadow_width(&style_btn_top, 0);

    /* Set the style of the bottom data buttons */
    lv_style_init(&style_btn_btm);
    lv_style_set_radius(&style_btn_btm, 2);
    lv_style_set_opa(&style_btn_btm, LV_OPA_100);
    lv_style_set_bg_color(&style_btn_btm, lv_palette_darken(LV_PALETTE_GREY, 4));
    lv_style_set_text_color(&style_btn_btm, lv_palette_lighten(LV_PALETTE_GREY, 4));
    lv_style_set_text_font(&style_btn_btm, &digital_display_140);
    lv_style_set_text_letter_space(&style_btn_btm, 0);
    lv_style_set_shadow_width(&style_btn_btm, 0);

    /* Darken button when pressed */
    static lv_color_filter_dsc_t color_filter_btn;
    lv_color_filter_dsc_init(&color_filter_btn, darken);
    lv_style_init(&style_btn_pressed);
    lv_style_set_color_filter_dsc(&style_btn_pressed, &color_filter_btn);
    lv_style_set_color_filter_opa(&style_btn_pressed, LV_OPA_20);

    /* Set the style of the top row label object */
    lv_style_init(&style_label_top);
    lv_style_set_text_color(&style_label_top, lv_palette_lighten(LV_PALETTE_GREY, 5));
    lv_style_set_text_font(&style_label_top, &vw_16);
    lv_style_set_text_letter_space(&style_label_top, 0);

    /* Set the style of the bottom row label object */
    lv_style_init(&style_label_btm);
    lv_style_set_text_color(&style_label_btm, lv_palette_lighten(LV_PALETTE_GREY, 5));
    lv_style_set_text_font(&style_label_btm, &vw_18);
    lv_style_set_text_letter_space(&style_label_btm, 0);
}

/**************
 * Transforms *
 **************/

lv_color_t darken(const lv_color_filter_dsc_t *dsc, lv_color_t color, lv_opa_t opa)
{
    LV_UNUSED(dsc);
    return lv_color_darken(color, opa);
}

/*****************
 * Layout Screen *
 *****************/

void open_layout_screen()
{
    lv_screen_load(layout_screen);
}

void setup_layout_screen()
{
    /***** Screen *****/
    layout_screen = lv_obj_create(NULL);                     // Create the Layout (Main) window in the background
    lv_obj_set_size(layout_screen, LCD_HORZ, LCD_VERT);      // Set to the proper window size
    lv_obj_center(layout_screen);                            // Puts window in the center of the display
    lv_obj_add_style(layout_screen, style_layout_screen, 0); // Add the style to the window
    /* Set grid layout type */
    lv_obj_set_grid_dsc_array(layout_screen, col_layout, row_layout); // Adds columns and rows to the window
    /* Remove padding around all objects to fit them closer together */
    lv_obj_set_style_pad_all(layout_screen, 0, 0);    // Remove padding around border of display
    lv_obj_set_style_pad_column(layout_screen, 0, 0); // Remove padding from columns between objects
    lv_obj_set_style_pad_row(layout_screen, 0, 0);    // Remove padding from rows between objects
    /* Turn off scrolling */
    lv_obj_set_scrollbar_mode(layout_screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(layout_screen, LV_OBJ_FLAG_SCROLLABLE);
    /***** Top Left Panel *****/
    /* Button */
    // layout.panel_tl.btn = lv_btn_create(layout_screen);                                                  // Create the button in the Layout window
    // lv_obj_add_style(layout.panel_tl.btn, layout.panel_tl.style_btn, 0);                                        // Add the style for the button
    // lv_obj_add_style(layout.panel_tl.btn, layout.panel_tl.style_btn_pressed, LV_STATE_PRESSED);                 // Add the style for the button when pressed
    // lv_obj_set_grid_cell(layout.panel_tl.btn, LV_GRID_ALIGN_STRETCH, 0, 2, LV_GRID_ALIGN_STRETCH, 0, 1); // Organize the Panel into the correct spot in the grid
    // /* Data in Button */
    // layout.panel_tl.data_btn = lv_label_create(layout.panel_tl.btn);                                 // Create a spot for data to go into the button
    // lv_label_set_text(layout.panel_tl.data_btn, layout.panel_tl.panel_info->get_data_str().c_str()); // Put the data in the Information object into the button
    // lv_obj_align(layout.panel_tl.data_btn, LV_ALIGN_CENTER, 0, 0);                            // Put the data into the center of the button
    // /* Label */
    // layout.panel_tl.label = lv_label_create(layout_screen);                                              // Create a label to show what data the button is holding
    // lv_label_set_text(layout.panel_tl.label, layout.panel_tl.panel_info->get_name().c_str());                   // Put the name of the Information object into the label
    // lv_obj_add_style(layout.panel_tl.label, layout.panel_tl.style_label, 0);                                    // Add the style for the label
    // lv_obj_set_grid_cell(layout.panel_tl.label, LV_GRID_ALIGN_CENTER, 0, 2, LV_GRID_ALIGN_CENTER, 1, 1); // Put the label right below the button
    // /***** Top Center Panel *****/
    // /* Button */
    // layout.panel_tc.btn = lv_btn_create(layout_screen);                                                  // Create the button in the Layout window
    // lv_obj_add_style(layout.panel_tc.btn, layout.panel_tc.style_btn, 0);                                        // Add the style for the button
    // lv_obj_add_style(layout.panel_tc.btn, layout.panel_tc.style_btn_pressed, LV_STATE_PRESSED);                 // Add the style for the button when pressed
    // lv_obj_set_grid_cell(layout.panel_tc.btn, LV_GRID_ALIGN_STRETCH, 0, 2, LV_GRID_ALIGN_STRETCH, 0, 1); // Organize the Panel into the correct spot in the grid
    // /* Data in Button */
    // layout.panel_tc.data_btn = lv_label_create(layout.panel_tc.btn);                                 // Create a spot for data to go into the button
    // lv_label_set_text(layout.panel_tc.data_btn, layout.panel_tc.panel_info->get_data_str().c_str()); // Put the data in the Information object into the button
    // lv_obj_align(layout.panel_tc.data_btn, LV_ALIGN_CENTER, 0, 0);                            // Put the data into the center of the button
    // /* Label */
    // layout.panel_tc.label = lv_label_create(layout_screen);                                              // Create a label to show what data the button is holding
    // lv_label_set_text(layout.panel_tc.label, layout.panel_tc.panel_info->get_name().c_str());                   // Put the name of the Information object into the label
    // lv_obj_add_style(layout.panel_tc.label, layout.panel_tc.style_label, 0);                                    // Add the style for the label
    // lv_obj_set_grid_cell(layout.panel_tc.label, LV_GRID_ALIGN_CENTER, 0, 2, LV_GRID_ALIGN_CENTER, 1, 1); // Put the label right below the button
    // /***** Top Right Panel *****/
    // /* Button */
    // layout.panel_tr.btn = lv_btn_create(layout_screen);                                                  // Create the button in the Layout window
    // lv_obj_add_style(layout.panel_tr.btn, layout.panel_tr.style_btn, 0);                                        // Add the style for the button
    // lv_obj_add_style(layout.panel_tr.btn, layout.panel_tr.style_btn_pressed, LV_STATE_PRESSED);                 // Add the style for the button when pressed
    // lv_obj_set_grid_cell(layout.panel_tr.btn, LV_GRID_ALIGN_STRETCH, 0, 2, LV_GRID_ALIGN_STRETCH, 0, 1); // Organize the Panel into the correct spot in the grid
    // /* Data in Button */
    // layout.panel_tr.data_btn = lv_label_create(layout.panel_tr.btn);                                 // Create a spot for data to go into the button
    // lv_label_set_text(layout.panel_tr.data_btn, layout.panel_tr.panel_info->get_data_str().c_str()); // Put the data in the Information object into the button
    // lv_obj_align(layout.panel_tr.data_btn, LV_ALIGN_CENTER, 0, 0);                            // Put the data into the center of the button
    // /* Label */
    // layout.panel_tr.label = lv_label_create(layout_screen);                                              // Create a label to show what data the button is holding
    // lv_label_set_text(layout.panel_tr.label, layout.panel_tr.panel_info->get_name().c_str());                   // Put the name of the Information object into the label
    // lv_obj_add_style(layout.panel_tr.label, layout.panel_tr.style_label, 0);                                    // Add the style for the label
    // lv_obj_set_grid_cell(layout.panel_tr.label, LV_GRID_ALIGN_CENTER, 0, 2, LV_GRID_ALIGN_CENTER, 1, 1); // Put the label right below the button
    // /***** Bottom Left Panel *****/
    // /* Button */
    // layout.panel_bl.btn = lv_btn_create(layout_screen);                                                  // Create the button in the Layout window
    // lv_obj_add_style(layout.panel_bl.btn, layout.panel_bl.style_btn, 0);                                        // Add the style for the button
    // lv_obj_add_style(layout.panel_bl.btn, layout.panel_bl.style_btn_pressed, LV_STATE_PRESSED);                 // Add the style for the button when pressed
    // lv_obj_set_grid_cell(layout.panel_bl.btn, LV_GRID_ALIGN_STRETCH, 0, 2, LV_GRID_ALIGN_STRETCH, 0, 1); // Organize the Panel into the correct spot in the grid
    // /* Data in Button */
    // layout.panel_bl.data_btn = lv_label_create(layout.panel_bl.btn);                                 // Create a spot for data to go into the button
    // lv_label_set_text(layout.panel_bl.data_btn, layout.panel_bl.panel_info->get_data_str().c_str()); // Put the data in the Information object into the button
    // lv_obj_align(layout.panel_bl.data_btn, LV_ALIGN_CENTER, 0, 0);                            // Put the data into the center of the button
    // /* Label */
    // layout.panel_bl.label = lv_label_create(layout_screen);                                              // Create a label to show what data the button is holding
    // lv_label_set_text(layout.panel_bl.label, layout.panel_bl.panel_info->get_name().c_str());                   // Put the name of the Information object into the label
    // lv_obj_add_style(layout.panel_bl.label, layout.panel_bl.style_label, 0);                                    // Add the style for the label
    // lv_obj_set_grid_cell(layout.panel_bl.label, LV_GRID_ALIGN_CENTER, 0, 2, LV_GRID_ALIGN_CENTER, 1, 1); // Put the label right below the button
    // /***** Bottom Right Panel *****/
    // /* Button */
    // layout.panel_br.btn = lv_btn_create(layout_screen);                                                  // Create the button in the Layout window
    // lv_obj_add_style(layout.panel_br.btn, layout.panel_br.style_btn, 0);                                        // Add the style for the button
    // lv_obj_add_style(layout.panel_br.btn, layout.panel_br.style_btn_pressed, LV_STATE_PRESSED);                 // Add the style for the button when pressed
    // lv_obj_set_grid_cell(layout.panel_br.btn, LV_GRID_ALIGN_STRETCH, 0, 2, LV_GRID_ALIGN_STRETCH, 0, 1); // Organize the Panel into the correct spot in the grid
    // /* Data in Button */
    // layout.panel_br.data_btn = lv_label_create(layout.panel_br.btn);                                 // Create a spot for data to go into the button
    // lv_label_set_text(layout.panel_br.data_btn, layout.panel_br.panel_info->get_data_str().c_str()); // Put the data in the Information object into the button
    // lv_obj_align(layout.panel_br.data_btn, LV_ALIGN_CENTER, 0, 0);                            // Put the data into the center of the button
    // /* Label */
    // layout.panel_br.label = lv_label_create(layout_screen);                                              // Create a label to show what data the button is holding
    // lv_label_set_text(layout.panel_br.label, layout.panel_br.panel_info->get_name().c_str());                   // Put the name of the Information object into the label
    // lv_obj_add_style(layout.panel_br.label, layout.panel_br.style_label, 0);                                    // Add the style for the label
    // lv_obj_set_grid_cell(layout.panel_br.label, LV_GRID_ALIGN_CENTER, 0, 2, LV_GRID_ALIGN_CENTER, 1, 1); // Put the label right below the button
}

/******************
 * Update Display *
 ******************/

void top_left_timer_cb(lv_timer_t *timer)
{
}

void top_center_timer_cb(lv_timer_t *timer)
{
}

void top_right_timer_cb(lv_timer_t *timer)
{
}

void bottom_left_timer_cb(lv_timer_t *timer)
{
}

void bottom_right_timer_cb(lv_timer_t *timer)
{
}