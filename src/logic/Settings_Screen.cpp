#include "Screen.hpp"

/****************
 * Constructors *
 ****************/

Settings_Screen::Settings_Screen()
{
    // Need to set this to work with EEPROM
    set_brightness(BRIGHT_DAY, DFLT_DAY_BRIGHTNESS);
    set_brightness(BRIGHT_NIGHT, DFLT_NIGHT_BRIGHTNESS);
    set_brightness(BRIGHT_DAY);
}

/********************
 * Public Functions *
 ********************/

void Settings_Screen::init()
{
    this->screen = lv_obj_create(NULL);
    lv_obj_set_size(this->screen, LCD_HORZ, LCD_VERT);
    lv_obj_center(this->screen);
    lv_obj_add_style(this->screen, &style_screen, 0);

    this->day_icon   = lv_label_create(this->screen);
    this->night_icon = lv_label_create(this->screen);
}

void Settings_Screen::set_brightness(BrightMode mode)
{
    pinMode(PWM_PIN, OUTPUT);

    switch (mode)
    {
        case BRIGHT_NIGHT:
            analogWrite(PWM_PIN, this->brightness.night);
            break;

        case BRIGHT_DAY:
        default:
            analogWrite(PWM_PIN, this->brightness.day);
            break;
    }
}

void Settings_Screen::set_brightness(BrightMode mode, uint8_t percent)
{
    if (percent > 100)
    {
        percent = 100;
    }

    switch (mode)
    {
        case BRIGHT_NIGHT:
            this->brightness.night = (percent * MAX_BRIGHTNESS) / 100;
            break;

        case BRIGHT_DAY:
        default:
            this->brightness.day = (percent * MAX_BRIGHTNESS) / 100;
            break;
    }
}