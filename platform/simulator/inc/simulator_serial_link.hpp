/**
 * @file simulator_serial_link.hpp
 * @brief Simulator ISerialLink implementation, backed by a real host serial
 * port (not a stub) — see its .cpp for why.
 */
#pragma once

#include "platform.hpp"

class SimulatorSerialLink : public ISerialLink
{
    public:
        static SimulatorSerialLink &instance();

        void   begin(uint32_t baud) override;
        size_t read(uint8_t *buf, size_t len) override;
        size_t write(const uint8_t *buf, size_t len) override;

    private:
        SimulatorSerialLink() = default;

        int fd_ = -1;
};
