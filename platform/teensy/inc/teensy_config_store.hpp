/**
 * @file teensy_config_store.hpp
 * @brief Teensy IConfigStore implementation, backed by EEPROM.h.
 */
#pragma once

#include "platform.hpp"

class TeensyConfigStore : public IConfigStore
{
    public:
        static TeensyConfigStore &instance();

        bool   read(uint32_t offset, void *buf, size_t len) override;
        bool   write(uint32_t offset, const void *buf, size_t len) override;
        size_t capacity() const override;

    private:
        TeensyConfigStore() = default;
};
