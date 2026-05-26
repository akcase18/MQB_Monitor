#pragma once

#include <lvgl.h>

#include "styles.hpp"

lv_obj_t *create_new_screen();

typedef struct
{
        lv_obj_t *btn;
        lv_obj_t *label;
} done_btn_t;

done_btn_t create_done_btn(lv_obj_t *screen);
