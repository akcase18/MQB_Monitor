#include "Screen.hpp"

static constexpr uint32_t SETTINGS_PULL_ANIM_MS = 220;

/*==============================================================================
 Constructors
 =============================================================================*/

Settings_Screen::Settings_Screen()
{
    // Need to set this to work with EEPROM
    set_brightness(BRIGHT_DAY, DFLT_DAY_BRIGHTNESS);
    set_brightness(BRIGHT_NIGHT, DFLT_NIGHT_BRIGHTNESS);
    set_brightness(BRIGHT_DAY);
}

/*==============================================================================
 Public Functions
 =============================================================================*/

void Settings_Screen::init()
{
    this->screen = lv_obj_create(NULL);
    lv_obj_set_size(this->screen, LCD_HORZ, LCD_VERT);
    lv_obj_center(this->screen);
    lv_obj_add_style(this->screen, &style_screen, 0);

    this->brightness_slider = lv_slider_create(this->screen);
    lv_obj_center(this->brightness_slider);

    this->done = lv_btn_create(this->screen);
    lv_obj_add_style(this->done, &style_config_done, 0);
    this->done_label = lv_label_create(this->done);
    lv_obj_center(this->done_label);
    lv_label_set_text(this->done_label, LV_SYMBOL_OK);
}

void Settings_Screen::open()
{
    lv_screen_load_anim(this->screen,
                        LV_SCR_LOAD_ANIM_OVER_BOTTOM,
                        SETTINGS_PULL_ANIM_MS,
                        0,
                        false);

    lv_slider_set_value(
        this->brightness_slider, this->brightness.day, LV_ANIM_OFF);
}

void Settings_Screen::close()
{
    this->brightness.day = lv_slider_get_value(this->brightness_slider);
}

void Settings_Screen::set_brightness(BrightMode mode)
{
#ifdef USE_ARDUINO
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
#endif
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
