#include "Request.hpp"

static uint32_t request_unique_id = 0;

/****************
 * Constructors *
 ****************/

Request::Request(uint16_t b1, uint16_t b2, float off, float sc)
    : offset(off), scale(sc)
{
    uds_bytes = {
        .byte_1 = b1,
        .byte_2 = b2};
    uid = request_unique_id++;
}