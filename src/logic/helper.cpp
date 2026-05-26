#include "helper.hpp"

lv_obj_t *create_new_screen()
{
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_size(screen, LCD_X, LCD_Y);
    lv_obj_center(screen);
    lv_obj_add_style(screen, &style_screen, 0);

    return screen;
}

done_btn_t create_done_btn(lv_obj_t *screen)
{
    done_btn_t done_btn = {};

    done_btn.btn = lv_btn_create(screen);
    lv_obj_add_style(done_btn.btn, &style_config_done, 0);
    done_btn.label = lv_label_create(done_btn.btn);
    lv_obj_center(done_btn.label);
    lv_label_set_text(done_btn.label, LV_SYMBOL_OK);
    lv_obj_add_style(done_btn.btn, &style_btn_pressed, LV_STATE_PRESSED);

    return done_btn;
}