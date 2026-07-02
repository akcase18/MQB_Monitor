/**
 * @file scheduler.hpp
 * @brief Platform-agnostic periodic task scheduler. Replaces the old
 * LVGL-timer-based pacing (lv_timer_create/lv_timer_set_period) so CAN
 * polling cadence no longer depends on the GUI framework's event loop.
 * Fixed-capacity, no heap — see platform.hpp's IClock for the time source.
 */
#pragma once

#include <stdint.h>

#include "platform.hpp"

namespace core
{

using TaskId       = uint16_t;
using TaskCallback = void (*)(void *ctx);

class Scheduler
{
    public:
        static constexpr uint16_t MAX_TASKS    = 32;
        static constexpr TaskId   INVALID_TASK  = 0xFFFF;

        explicit Scheduler(IClock &clock);

        /**
         * Registers a task to run every `period_ms`, starting one period
         * from now.
         *
         * @returns The new task's id, or INVALID_TASK if capacity is full.
         */
        TaskId every(uint32_t period_ms, TaskCallback cb, void *ctx = nullptr);

        /**
         * Changes an existing task's period. Takes effect on its next run
         * (replaces the old per-Request stop/start-timer dance on panel
         * reassignment).
         */
        void set_period(TaskId id, uint32_t period_ms);

        /**
         * Runs any due tasks. Call once per main-loop iteration.
         */
        void tick();

    private:
        struct Task
        {
                TaskCallback cb          = nullptr;
                void        *ctx         = nullptr;
                uint32_t     period_ms   = 0;
                uint32_t     last_run_ms = 0;
                bool         active      = false;
        };

        IClock  &clock_;
        Task     tasks_[MAX_TASKS];
        uint16_t count_ = 0;
};

} // namespace core
