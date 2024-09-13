// #include "Information.hpp"
#include <FlexCAN_T4.h>

void can_init();
void can_loop();
void send_msg(Information info);
void recv_msg(const CAN_message_t &msg);
void convert_unit(Information info);
float round(float num, uint8_t decimal_places);
void round_data(Information info);