/**
 * @file teensy_can.hpp
 * @brief Teensy ICanBus implementation, backed by FlexCAN_T4.
 */
#pragma once

#include "platform.hpp"

class TeensyCanBus : public ICanBus
{
    public:
        static TeensyCanBus &instance();

        bool begin(uint32_t rx_id_lo, uint32_t rx_id_hi, uint32_t baud) override;
        bool send(const CanFrame &frame) override;
        bool receive(CanFrame &out) override;

        bool       sniff_begin(uint32_t baud, bool listen_only) override;
        void       sniff_end() override;
        bool       sniff_read(SniffFrame &out) override;
        bool       sniff_send(const SniffFrame &frame) override;
        SniffStats sniff_stats() const override;

    private:
        TeensyCanBus() = default;

        /**
         * begin()'s arguments, so sniff_end() can put them back.
         */
        uint32_t rx_id_lo_ = 0;
        uint32_t rx_id_hi_ = 0;
        uint32_t baud_     = 0;

        bool sniffing_    = false;
        bool listen_only_ = true;
};
