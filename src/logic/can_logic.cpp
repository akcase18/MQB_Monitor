// #include "can_logic.hpp"

// #define CAN_ID 0x7E0

// #define NO_OFFSET 0
// #define NO_SCALE 1

// /*******
//  * CAN *
//  *******/

// // Initialize CAN port
// FlexCAN_T4<CAN1, RX_SIZE_256, TX_SIZE_16> CAN;

// uint8_t can_count = 0;

// void can_init()
// {
//   CAN.begin();
//   CAN.setBaudRate(500000);
//   CAN.setMBFilter(REJECT_ALL);
//   CAN.setMBFilter(MB1, 0x7E8);
//   CAN.enableMBInterrupt(MB1);
//   CAN.onReceive(recv_msg);
// }

// void can_loop()
// {
//   CAN.events();
// }

// void send_msg(Information info)
// {
//   CAN_message_t msg;
//   msg.buf[0] = 0x03;
//   msg.buf[1] = 0x22;
//   msg.buf[2] = info.get_byte_1();
//   msg.buf[3] = info.get_byte_2();

//   msg.id = CAN_ID;

//   last_sent = info;

//   CAN.write(msg);
// }

// void recv_msg(const CAN_message_t &msg)
// {
//   Information info = last_sent;
//   if ((msg.buf[2] == info.get_byte_1()) & (msg.buf[3] == info.get_byte_2()))
//   {
//     int32_t x = 0;
//     if (msg.buf[0] == 0x04)
//     {
//       x = msg.buf[4];
//     }
//     else if (msg.buf[0] == 0x05)
//     {
//       x = (msg.buf[4] << 8) | msg.buf[5];
//     }
//     x = (x + info.get_offset()) * info.get_scale();
//     info.set_data(x);
//   }
// }

// void convert_unit(Information info)
// {
//   switch (info.get_unit_type())
//   {
//   case Information::PRESSURE:
//     info.set_data((info.get_data_num() * 14.5038));
//     break;
//   case Information::SPEED:
//     info.set_data((info.get_data_num() / 1.609));
//     break;
//   case Information::TEMP:
//     info.set_data((info.get_data_num() * (9 / 5)) + 32);
//     break;
//   case Information::TORQUE:
//     info.set_data((info.get_data_num() * 0.7375621493));
//     break;
//   case Information::VOLUME:
//     info.set_data((info.get_data_num() / 3.785));
//     break;
//   default:
//     break;
//   }
// }

// float round(float num, uint8_t decimal_places)
// {
//   int round_value = pow(10, decimal_places);

//   float val = (int)(num * round_value + 0.5);
//   return (float)(val / round_value);
// }

// void round_data(Information info)
// {
//   switch (info.get_unit_type())
//   {
//   case Information::VOLUME:
//     info.set_data(round(info.get_data_num(), 1));
//     break;
//   case Information::PRESSURE:
//     if (info.get_is_metric())
//     {
//       info.set_data(round(info.get_data_num(), 2));
//     }
//     else
//     {
//       info.set_data(round(info.get_data_num(), 1));
//     }
//     break;
//   case Information::TEMP:
//   case Information::SPEED:
//   case Information::PERCENT:
//   case Information::RPM:
//   case Information::TORQUE:
//   case Information::DEGREES:
//   case Information::NONE:
//   default:
//     info.set_data((int)info.get_data_num());
//     break;
//   }
// }