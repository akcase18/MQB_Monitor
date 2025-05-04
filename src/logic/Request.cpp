#include "Request.hpp"

static uint32_t request_unique_id = 0;

/****************
 * Constructors *
 ****************/

Request::Request(uint16_t b1, uint16_t b2, float off, float sc)
    : byte_1(b1), 
      byte_2(b2),
      offset(off), 
      scale(sc)
{
    uid = request_unique_id++;
}