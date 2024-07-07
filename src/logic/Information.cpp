#include "Information.hpp"

/********************
 * Public Functions *
 ********************/

Information::Information()
{

}

Information::Information(Information *info)
{

}

Information::Information(char *name, uint16_t byte_1, uint16_t byte_2, int16_t offset, float scale, Unit_Type unit_type, uint8_t num_decimals_metric, uint8_t num_decimals_imperial, uint16_t refresh_rate)
{

}

void Information::set_name(char *name)
{

}
char *Information::get_name()
{

}
char *Information::get_name_and_label()
{

}
void Information::set_data(float data)
{

}
float Information::get_data_num()
{

}
char *Information::get_data_str()
{

}
void Information::set_byte_1(uint16_t byte)
{

}
uint16_t Information::get_byte_1()
{

}
void Information::set_byte_2(uint16_t byte)
{

}
uint16_t Information::get_byte_2()
{

}
void Information::set_offset(int16_t offset)
{

}
int16_t Information::get_offset()
{

}
void Information::set_scale(float scale)
{

}
float Information::get_scale()
{

}
void Information::set_unit_type(Unit_Type type)
{

}
Information::Unit_Type Information::get_unit_type()
{

}
void Information::set_unit_region(Unit_Region region)
{

}
Information::Unit_Region Information::get_unit_region()
{

}

void Information::set_num_decimals_metric(uint8_t decimals)
{

}
uint8_t Information::get_num_decimals_metric()
{

}

void Information::set_num_decimals_imperial(uint8_t decimals)
{

}
uint8_t Information::get_num_decimals_imperial()
{

}

void Information::set_refresh_rate(uint16_t refresh_rate)
{

}
uint16_t Information::get_refresh_rate()
{

}