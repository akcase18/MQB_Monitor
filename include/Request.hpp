#pragma once

#include "Arduino.h"
#include "etl_hash.hpp"
#include "styles.hpp"

constexpr uint8_t NO_OFFSET = 0;
constexpr uint8_t NO_SCALE = 1;
constexpr uint8_t NUM_REQS = 24;

class Request
{
public:
    /* Public Variables */
    uint32_t uid;            // Unique ID for the Request
    uint16_t byte_1;         // First byte of UDS request code (in hex)
    uint16_t byte_2;         // Second byte of UDS request code (in hex)
    float offset;            // Offset value to decode raw data
    float scale;             // Scaling factor to decode raw data
    float data = 0;          // Received data (decoded)
    lv_timer_t *timer;       // Timer for the Request
    bool timer_init = false; // True if timer is initialized, otherwise false

    /**
     * Constructor for Request objects.
     * Creates a new Request that will be sent to the car
     * in order to get data from it.
     *
     * @param uds_data UDS request bytes
     * @param off the offset value for decoding raw data
     * @param sc the scaling factor for decoding raw data
     */
    Request(uint16_t b1, uint16_t b2, float off, float sc);

    /**
     * Start LVGL timer.
     */
    void start_timer(lv_timer_cb_t cb, uint32_t period);

    /**
     * Stop LVGL timer.
     */
    void stop_timer();
};

extern etl::unordered_map<etl::pair<uint16_t, uint16_t>, Request *, NUM_REQS> request_list;