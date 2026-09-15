#include "sensor_filter.hpp"

#include <math.h>

namespace core
{

    namespace
    {
        float median3(float a, float b, float c)
        {
            if (a > b)
            {
                float t = a;
                a       = b;
                b       = t;
            }
            if (b > c)
            {
                b = c;
            }
            return a > b ? a : b;
        }
    } // namespace

    void SensorFilter::configure(float tau_s)
    {
        tau_s_ = tau_s > 0.0f ? tau_s : 0.0f;
    }

    void SensorFilter::reset()
    {
        count_  = 0;
        primed_ = false;
        ema_    = 0.0f;
    }

    float SensorFilter::update(float sample, uint32_t dt_ms)
    {
        history_[2] = history_[1];
        history_[1] = history_[0];
        history_[0] = sample;
        if (count_ < 3)
        {
            count_++;
        }

        /**
         * Before three samples exist there is nothing to take a median of,
         * so the newest sample stands. Deliberately not "wait for three":
         * a gauge that shows nothing for two extra sample periods after a
         * fault clears reads as a fault that has not cleared. */
        float filtered = count_ >= 3
                             ? median3(history_[0], history_[1], history_[2])
                             : sample;

        if (!primed_)
        {
            ema_    = filtered;
            primed_ = true;
            return ema_;
        }

        if (tau_s_ <= 0.0f || dt_ms == 0)
        {
            ema_ = filtered;
            return ema_;
        }

        /**
         * alpha = 1 - e^(-dt/tau): the sampled form of a first-order lag,
         * so the time constant means the same thing whatever the cadence
         * happens to be. */
        float dt_s  = static_cast<float>(dt_ms) / 1000.0f;
        float alpha = 1.0f - expf(-dt_s / tau_s_);

        ema_ += alpha * (filtered - ema_);
        return ema_;
    }

    bool SensorFilter::has_value() const
    {
        return primed_;
    }

    float SensorFilter::value() const
    {
        return ema_;
    }

} // namespace core
