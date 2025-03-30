#include "Arduino.h"

#define NO_OFFSET 0
#define NO_SCALE 1

uint32_t req_uid = 0;

struct Request
{
    static inline uint32_t gen_uid()
    {
        static uint32_t req_uid = 0;
        return req_uid++;
    }

    uint32_t uid;
    uint16_t byte_1; // First byte of UDS request code (in hex)
    uint16_t byte_2; // Second byte of UDS request code (in hex)
    float offset;    // Offset value to decode raw data
    float scale;     // Scaling factor to decode raw data
    float data = 0;  // Received data (decoded)

    Request(uint32_t b1, uint32_t b2, float off, float sc)
        : uid(gen_uid()), byte_1(b1), byte_2(b2), offset(off), scale(sc) {}
};