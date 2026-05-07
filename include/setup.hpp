#pragma once

#include "styles.hpp"
#include "transforms.hpp"
#include "Request.hpp"
#include "Info.hpp"
#include "Panel.hpp"
#include "Screen.hpp"
#include "can_handler.hpp"
#include "Ring_Buffer.hpp"

/*==============================================================================
 Constants
 =============================================================================*/

constexpr uint8_t NUM_INFO   = 22;
constexpr uint8_t NUM_PANELS = 5;

/*==============================================================================
 Panels
 =============================================================================*/

extern Panel panel_1;
extern Panel panel_2;
extern Panel panel_3;
extern Panel panel_4;
extern Panel panel_5;
extern Panel panel_6;

extern Panel *panel_list[NUM_PANELS];

/*==============================================================================
 Screens
 =============================================================================*/

extern Settings_Screen settings;
extern Info_Screen     screen_1;
extern Configurator    config;

/*==============================================================================
 CAN
 =============================================================================*/

extern lv_timer_t *can_timer;

/*==============================================================================
 Initialization
 =============================================================================*/

void can_push_cb(lv_timer_t *timer);

void can_send_cb(lv_timer_t *timer);

void refresh_request_timers();

void init_timers();

void init_requests();

void init_panels(EEPROM_Handler &cfg);

void init_screens();

void init_transforms();

void setup_init(EEPROM_Handler &cfg);
