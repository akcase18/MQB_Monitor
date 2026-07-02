/**
 * @file teensy_config_store.hpp
 * @brief Teensy IConfigStore implementation. Phase 1 stub — no EEPROM.h
 * wiring yet, see teensy_config_store.cpp.
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
