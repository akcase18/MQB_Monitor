#ifndef PANEL_H
#define PANEL_H

#include <lvgl.h>
#include "Information.hpp"

using namespace std;

/********************
 * Global Constants *
 ********************/

class Panel
{
private:
    /*********************
     * Private Constants *
     *********************/

public:
    /********************
     * Public Constants *
     ********************/

private:
    /*********************
     * Private Variables *
     *********************/

    /*********************
     * Private Functions *
     *********************/

public:
    /********************
     * Public Variables *
     ********************/

    static Information panel_info;
    static lv_obj_t *button;
    static lv_obj_t *button_data;
    static lv_obj_t *label;
    static lv_style_t *button_style;
    static lv_style_t *label_style;

    /********************
     * Public Functions *
     ********************/

    Panel();

    Panel(Information *info);

    void set_button_style(lv_style_t *style);
    lv_style_t *get_button_style();

    void set_label_style(lv_style_t *style);
    lv_style_t *get_label_style();
};

#endif