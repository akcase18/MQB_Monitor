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

void Request::start_timer(lv_timer_cb_t cb, uint32_t period)
{
    if (!timer_init)
    {
        this->timer = lv_timer_create(cb, period, this);
        timer_init = true;
    }
    else
    {
        lv_timer_resume(this->timer);
    }
}

void Request::stop_timer()
{
    lv_timer_pause(this->timer);
}