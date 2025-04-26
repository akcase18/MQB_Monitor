#pragma once

#include "Arduino.h"
#include "etl/unordered_map.h"
#include "styles.hpp"

constexpr uint8_t NO_OFFSET = 0;
constexpr uint8_t NO_SCALE = 1;

struct UDS_Data
{
    uint16_t byte_1; // First byte of UDS request code (in hex)
    uint16_t byte_2; // Second byte of UDS request code (in hex)
};

class Request
{
public:
    /* Public Variables */
    uint32_t uid;       // Unique ID for the Request
    UDS_Data uds_bytes; // UDS bytes for the Request
    float offset;       // Offset value to decode raw data
    float scale;        // Scaling factor to decode raw data
    float data = 0;     // Received data (decoded)

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
};

// etl::unordered_map<UDS_Data, Request *, NUM_REQS> requests;