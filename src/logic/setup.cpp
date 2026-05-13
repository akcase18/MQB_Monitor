#include "setup.hpp"

/*==============================================================================
 Requests
 =============================================================================*/

/***** Performance Metrics *****/
Request cool_tmp_req
    = Request(0xF4, 0x05, -40, NO_SCALE); // Coolant Temperature
Request eng_oil_tmp_req
    = Request(0x40, 0xD5, NO_OFFSET, 0.1); // Engine Oil Temperature
Request int_air_tmp_req
    = Request(0xF4, 0x0F, -40, NO_SCALE); // Intake Air Temperature
Request chg_pres_req = Request(0x20, 0x2a, NO_OFFSET, 0.001); // Charge Pressure
Request int_pres_req = Request(0x3E, 0x70, NO_OFFSET, 0.001); // Intake Pressure
Request amb_pres_req
    = Request(0x13, 0xCA, NO_OFFSET, 0.0001); // Ambient Pressure
Request lmd_act_req
    = Request(0x10, 0xC0, NO_OFFSET, (1 / 1024)); // Actual Lambda
Request lmd_spec_req
    = Request(0x29, 0x50, NO_OFFSET, (1 / 1024));            // Specified Lambda
Request trbo_spd_req = Request(0x10, 0x40, NO_OFFSET, 6.11); // Turbo Speed

/***** Car Information *****/
Request eng_rpm_req = Request(0x20, 0x6E, NO_OFFSET, NO_SCALE); // Engine RPM
Request eng_trq_req = Request(0x43, 0x7C, NO_OFFSET, 0.1);      // Engine Torque
Request car_spd_req = Request(0x20, 0x33, NO_OFFSET, 0.01);     // Vehicle Speed
Request fuel_lvl_req
    = Request(0x22, 0x06, NO_OFFSET, NO_SCALE); // Fuel (Volume)
Request fuel_pcnt_req
    = Request(0xF4, 0x2F, NO_OFFSET, (1 / 2.55)); // Fuel (Percentage)
Request thrtl_pos_req
    = Request(0xF4, 0x11, NO_OFFSET, (1 / 2.55)); // Throttle Position

/***** Diag/Monitoring *****/
Request msfr_sum_req
    = Request(0x29, 0x04, NO_OFFSET, NO_SCALE); // Misfires (Sum)
Request msfr_1_req
    = Request(0x29, 0x66, NO_OFFSET, NO_SCALE); // Misfires (Cylinder 1)
Request msfr_2_req
    = Request(0x29, 0x67, NO_OFFSET, NO_SCALE); // Misfires (Cylinder 2)
Request msfr_3_req
    = Request(0x29, 0x68, NO_OFFSET, NO_SCALE); // Misfires (Cylinder 3)
Request msfr_4_req
    = Request(0x29, 0x69, NO_OFFSET, NO_SCALE); // Misfires (Cylinder 4)
Request tim_cor_1_req
    = Request(0x20, 0x0A, NO_OFFSET, 0.01); // Timing Correction (Cylinder 1)
Request tim_cor_2_req
    = Request(0x20, 0x0B, NO_OFFSET, 0.01); // Timing Correction (Cylinder 2)
Request tim_cor_3_req
    = Request(0x20, 0x0C, NO_OFFSET, 0.01); // Timing Correction (Cylinder 3)
Request tim_cor_4_req
    = Request(0x20, 0x0D, NO_OFFSET, 0.01); // Timing Correction (Cylinder 4)

Request *request_list[NUM_REQS] = {};

/*==============================================================================
 Info
 =============================================================================*/

/* Coolant Temp */
Request *coolant_temp_req_list[] = { &cool_tmp_req };
Info     coolant_temp
    = Info("Cool Temp", Unit_Type::Temp, 2000, coolant_temp_req_list, 1);

/* Engine Oil Temp */
Request *engine_oil_temp_req_list[] = { &eng_oil_tmp_req };
Info     engine_oil_temp
    = Info("Eng Oil Temp", Unit_Type::Temp, 1000, engine_oil_temp_req_list, 1);

/* Intake Air Temp */
Request *intake_air_temp_req_list[] = { &int_air_tmp_req };
Info     intake_air_temp
    = Info("Int Air Temp", Unit_Type::Temp, 100, intake_air_temp_req_list, 1);

/* Charge Pressure */
Request *charge_pressure_req_list[] = { &chg_pres_req };
Info     charge_pressure            = Info(
    "Charge Pres", Unit_Type::Pressure, 100, charge_pressure_req_list, 1);

/* Intake Pressure */
Request *intake_pressure_req_list[] = { &int_pres_req };
Info     intake_pressure
    = Info("Int Pres", Unit_Type::Pressure, 20, intake_pressure_req_list, 1);

/* Ambient Pressure */
Request *ambient_pressure_req_list[] = { &amb_pres_req };
Info     ambient_pressure
    = Info("Amb Pres", Unit_Type::Pressure, 1000, ambient_pressure_req_list, 1);

/* Actual Intake Pressure */
Request *actual_intake_pressure_req_list[] = { &amb_pres_req, &int_pres_req };
Info     actual_intake_pressure = Info("Int Pres Act.",
                                       Unit_Type::Pressure,
                                       20,
                                       actual_intake_pressure_req_list,
                                       2);

/* Engine RPM */
Request *engine_rpm_req_list[] = { &eng_rpm_req };
Info     engine_rpm
    = Info("Engine Speed", Unit_Type::RPM, 20, engine_rpm_req_list, 1);

/* Engine Torque */
Request *engine_torque_req_list[] = { &eng_trq_req };
Info     engine_torque
    = Info("Engine Torque", Unit_Type::Torque, 20, engine_torque_req_list, 1);

/* Vehicle Speed */
Request *vehicle_speed_req_list[] = { &car_spd_req };
Info     vehicle_speed
    = Info("Speed", Unit_Type::Speed, 100, vehicle_speed_req_list, 1);

/* Fuel Level */
Request *fuel_level_req_list[] = { &fuel_lvl_req };
Info fuel_level = Info("Fuel", Unit_Type::Volume, 5000, fuel_level_req_list, 1);

/* Fuel Percent */
Request *fuel_percent_req_list[] = { &fuel_pcnt_req };
Info     fuel_percent
    = Info("Fuel", Unit_Type::Percent, 5000, fuel_percent_req_list, 1);

/* Throttle Position */
Request *throttle_position_req_list[] = { &thrtl_pos_req };
Info     throttle_position            = Info(
    "Throttle Pos", Unit_Type::Percent, 20, throttle_position_req_list, 1);

/* Misfires */
Request *misfire_sum_req_list[] = { &msfr_sum_req };
Info     misfire_sum
    = Info("Misfire Cnt", Unit_Type::None, 500, misfire_sum_req_list, 1);

/* Cyl 1 Misfires */
Request *misfire_cyl_1_req_list[] = { &msfr_1_req };
Info     misfire_cyl_1            = Info(
    "Misfire Cnt Cyl 1", Unit_Type::None, 500, misfire_cyl_1_req_list, 1);

/* Cyl 2 Misfires */
Request *misfire_cyl_2_req_list[] = { &msfr_2_req };
Info     misfire_cyl_2            = Info(
    "Misfire Cnt Cyl 2", Unit_Type::None, 500, misfire_cyl_2_req_list, 1);

/* Cyl 3 Misfires */
Request *misfire_cyl_3_req_list[] = { &msfr_3_req };
Info     misfire_cyl_3            = Info(
    "Misfire Cnt Cyl 3", Unit_Type::None, 500, misfire_cyl_3_req_list, 1);

/* Cyl 4 Misfires */
Request *misfire_cyl_4_req_list[] = { &msfr_4_req };
Info     misfire_cyl_4            = Info(
    "Misfire Cnt Cyl 4", Unit_Type::None, 500, misfire_cyl_4_req_list, 1);

/* Cyl 1 Timing Correction */
Request *timing_corr_1_req_list[] = { &tim_cor_1_req };
Info     timing_corr_1            = Info(
    "Time Corr. Cyl 1", Unit_Type::Degrees, 100, timing_corr_1_req_list, 1);

/* Cyl 2 Timing Correction */
Request *timing_corr_2_req_list[] = { &tim_cor_2_req };
Info     timing_corr_2            = Info(
    "Time Corr. Cyl 2", Unit_Type::Degrees, 100, timing_corr_2_req_list, 1);

/* Cyl 3 Timing Correction */
Request *timing_corr_3_req_list[] = { &tim_cor_3_req };
Info     timing_corr_3            = Info(
    "Time Corr. Cyl 3", Unit_Type::Degrees, 100, timing_corr_3_req_list, 1);

/* Cyl 4 Timing Correction */
Request *timing_corr_4_req_list[] = { &tim_cor_4_req };
Info     timing_corr_4            = Info(
    "Time Corr. Cyl 4", Unit_Type::Degrees, 100, timing_corr_4_req_list, 1);

Info *info_list[NUM_INFO]
    = { &coolant_temp,           &engine_oil_temp, &intake_air_temp,
        &charge_pressure,        &intake_pressure, &ambient_pressure,
        &actual_intake_pressure, &engine_rpm,      &engine_torque,
        &vehicle_speed,          &fuel_level,      &fuel_percent,
        &throttle_position,      &misfire_sum,     &misfire_cyl_1,
        &misfire_cyl_2,          &misfire_cyl_3,   &misfire_cyl_4,
        &timing_corr_1,          &timing_corr_2,   &timing_corr_3,
        &timing_corr_4 };

/*==============================================================================
 Panels
 =============================================================================*/

Panel panel_1 = Panel(); // coolant temp
Panel panel_2 = Panel(); // throttle position
Panel panel_3 = Panel(); // intake air temp
Panel panel_4 = Panel(); // engine oil temp
Panel panel_5 = Panel(); // actual intake pressure
Panel panel_6 = Panel(); // fuel (%)

Panel *panel_list[NUM_PANELS]
    = { &panel_1, &panel_2, &panel_3, &panel_4, &panel_5 };

/*==============================================================================
 Screens
 =============================================================================*/

Settings_Screen settings = Settings_Screen();
Info_Screen     screen_1 = Info_Screen(panel_list, NUM_PANELS);
Configurator    config   = Configurator(info_list, NUM_INFO);

/*==============================================================================
 CAN
 =============================================================================*/

RingBuffer<Request *, 64> can_queue;
lv_timer_t               *can_timer;

/*==============================================================================
 Initialization
 =============================================================================*/

void can_push_cb(lv_timer_t *timer)
{
    Request *request = (Request *)lv_timer_get_user_data(timer);
    can_queue.put(request);
}

void can_send_cb(lv_timer_t *timer)
{
    if (!can_queue.empty())
    {
#ifdef USE_ARDUINO
        can_send(can_queue.get());
#else
        can_queue.get();
#endif
    }
}

void refresh_request_timers()
{
    Request *active_requests[NUM_REQS] = {};
    uint32_t active_periods[NUM_REQS]  = {};
    uint8_t  active_count              = 0;

    for (uint8_t i = 0; i < NUM_REQS; i++)
    {
        request_list[i]->stop_timer();
    }

    can_queue.reset();

    for (uint8_t i = 0; i < NUM_PANELS; i++)
    {
        Panel *panel = panel_list[i];

        for (uint8_t ii = 0; ii < panel->info->get_num_requests(); ii++)
        {
            Request *request       = panel->info->get_requests()[ii];
            uint32_t period        = panel->info->get_cycle_time();
            bool     found_request = false;

            for (uint8_t iii = 0; iii < active_count; iii++)
            {
                if (active_requests[iii] == request)
                {
                    if (period < active_periods[iii])
                    {
                        active_periods[iii] = period;
                    }
                    found_request = true;
                    break;
                }
            }

            if (!found_request && active_count < NUM_REQS)
            {
                active_requests[active_count] = request;
                active_periods[active_count]  = period;
                active_count++;
            }
        }
    }

    for (uint8_t i = 0; i < active_count; i++)
    {
        active_requests[i]->start_timer(can_push_cb, active_periods[i]);
    }
}

void init_timers()
{
    can_timer = lv_timer_create(can_send_cb, 10, NULL);
    refresh_request_timers();
}

void init_requests()
{
    request_list[0]  = &cool_tmp_req;
    request_list[1]  = &eng_oil_tmp_req;
    request_list[2]  = &int_air_tmp_req;
    request_list[3]  = &chg_pres_req;
    request_list[4]  = &int_pres_req;
    request_list[5]  = &amb_pres_req;
    request_list[6]  = &lmd_act_req;
    request_list[7]  = &lmd_spec_req;
    request_list[8]  = &trbo_spd_req;
    request_list[9]  = &eng_rpm_req;
    request_list[10] = &eng_trq_req;
    request_list[11] = &car_spd_req;
    request_list[12] = &fuel_lvl_req;
    request_list[13] = &fuel_pcnt_req;
    request_list[14] = &thrtl_pos_req;
    request_list[15] = &msfr_sum_req;
    request_list[16] = &msfr_1_req;
    request_list[17] = &msfr_2_req;
    request_list[18] = &msfr_3_req;
    request_list[19] = &msfr_4_req;
    request_list[20] = &tim_cor_1_req;
    request_list[21] = &tim_cor_2_req;
    request_list[22] = &tim_cor_3_req;
    request_list[23] = &tim_cor_4_req;
}

void init_panels(EEPROM_Handler &cfg)
{
    panel_1.set_info(info_list[cfg.config.panels[0].panel_info]);
    panel_2.set_info(info_list[cfg.config.panels[1].panel_info]);
    panel_3.set_info(info_list[cfg.config.panels[2].panel_info]);
    panel_4.set_info(info_list[cfg.config.panels[3].panel_info]);
    panel_5.set_info(info_list[cfg.config.panels[4].panel_info]);

    // panel_1.set_info(&coolant_temp);
    // panel_2.set_info(&throttle_position);
    // panel_3.set_info(&intake_air_temp);
    // panel_4.set_info(&engine_oil_temp);
    // panel_5.set_info(&actual_intake_pressure);
}

void init_screens()
{
    screen_1.init(5);
    config.init();
    settings.init();
}

void init_transforms()
{
    for (uint8_t i = 0; i < NUM_INFO; i++)
    {
        info_list[i]->set_transform(transform_default);
    }
    actual_intake_pressure.set_transform(transform_act_intake_pressure);
}

void setup_init(EEPROM_Handler &cfg)
{
    init_screens();
    init_panels(cfg);
    init_transforms();
    init_requests();
    init_timers();
}
