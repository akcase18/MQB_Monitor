#pragma once

#include "Request.hpp"

float transform_default(Request **reqs)
{
    return (reqs[0]->data);
}

float transform_act_intake_pressure(Request **reqs)
{
    return (reqs[0]->data) - (reqs[1]->data);
}