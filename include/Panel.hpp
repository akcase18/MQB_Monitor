#pragma once

/**
 * In charge of displaying (and formatting) a single Info
 * object.
 *
 * Needs to be able to display with numbers (primary) and
 * some sort of bar or arc (secondary).
 */

#include "Info.hpp"
#include "styles.hpp"
#include "EEPROM_Handler.hpp"

/**
 * The regular Panel class will handle numeric display
 * of information.
 */
class Panel
{
public:
    uint32_t uid; // Unique ID for the Panel
    bool initialized = false;
    Info *info;
    lv_obj_t *btn;
    lv_obj_t *data;
    lv_obj_t *label;
    uint8_t panel_num = NULL; // The panel number (0-indexed)

    Panel();

    void init(lv_obj_t *parent);

    void set_info(Info *new_info);

    void update();

    void set_sm_panel();
    void set_lg_panel();
};


/**
 * The Graphic Panel class will add in an image of some
 * kind, most likely an arc (or similar), to help display
 * the information
 */
class Graphic_Panel : public Panel
{
public:
    lv_obj_t *graphic;

    Graphic_Panel();
};