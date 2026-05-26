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
    this->screen = create_new_screen();

    this->brightness_slider = lv_slider_create(this->screen);
    lv_obj_center(this->brightness_slider);

    done_btn_t done_btn = create_done_btn(this->screen);
    this->done          = done_btn.btn;
    this->done_label    = done_btn.label;
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
