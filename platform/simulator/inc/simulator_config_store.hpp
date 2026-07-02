/**
 * @file simulator_config_store.hpp
 * @brief Simulator IConfigStore implementation, backed by a local file.
 */
#pragma once

#include "platform.hpp"

class SimulatorConfigStore : public IConfigStore
{
    public:
        static SimulatorConfigStore &instance();

        bool   read(uint32_t offset, void *buf, size_t len) override;
        bool   write(uint32_t offset, const void *buf, size_t len) override;
        size_t capacity() const override;

    private:
        SimulatorConfigStore() = default;
};
