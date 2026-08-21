#include "scheduler.hpp"

namespace core
{

    Scheduler::Scheduler(IClock &clock)
        : clock_(clock)
    {
    }

    TaskId Scheduler::every(uint32_t period_ms, TaskCallback cb, void *ctx)
    {
        if (count_ >= MAX_TASKS)
        {
            return INVALID_TASK;
        }

        TaskId id   = count_;
        Task  &task = tasks_[id];

        task.cb          = cb;
        task.ctx         = ctx;
        task.period_ms   = period_ms;
        task.last_run_ms = clock_.millis();
        count_++;

        return id;
    }

    void Scheduler::tick()
    {
        uint32_t now = clock_.millis();

        for (uint16_t i = 0; i < count_; i++)
        {
            Task &task = tasks_[i];

            /* Unsigned subtraction wraps correctly across millis() overflow. */
            if ((now - task.last_run_ms) >= task.period_ms)
            {
                task.last_run_ms = now;
                task.cb(task.ctx);
            }
        }
    }

} // namespace core
