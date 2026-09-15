/**
 * @file simulator_analog_input.hpp
 * @brief IAnalogInput over a scripted engine-warmup model.
 *
 * Four channels, matching the gauge product's planned ADS1115 assignment:
 *
 *   0  oil pressure   0.5-4.5V ratiometric transducer, via a 0.6x divider
 *   1  oil temp       NTC thermistor against a pull-up
 *   2  coolant temp   NTC thermistor against a pull-up
 *   3  5V rail sense  the transducer's supply, via the same 0.6x divider
 *
 * The waveforms are scripted in *engineering* units -- psi and degrees C --
 * and then pushed through the inverse of each sensor's real transfer
 * function to produce the voltage the ADC would actually see. That is the
 * point: core/sensors' conditioning has to run that conversion forwards and
 * land back on the number this file started from. A simulator that emitted
 * convenient voltages directly would let a wrong transfer function pass.
 *
 * Deterministic, like SimulatorCanBus: a function of elapsed time only, no
 * RNG, so a run reproduces exactly.
 */
#pragma once

#include "platform.hpp"

class SimulatorAnalogInput : public IAnalogInput
{
    public:
        static constexpr uint8_t CHANNEL_COUNT = 4;

        /**
         * Channel assignment, named so the waveform code reads as something
         * other than magic indices. Nothing above platform/ uses these --
         * up there, which channel feeds which signal is configuration. */
        static constexpr uint8_t CH_OIL_PRESSURE = 0;
        static constexpr uint8_t CH_OIL_TEMP     = 1;
        static constexpr uint8_t CH_COOLANT_TEMP = 2;
        static constexpr uint8_t CH_RAIL_SENSE   = 3;

        static SimulatorAnalogInput &instance();

        void    begin() override;
        uint8_t channel_count() const override;
        bool    read_microvolts(uint8_t ch, uint32_t &out) override;

        /**
         * ----------------------------------------------------------------
         * The model, as pure functions of time.
         *
         * read_microvolts() is just these plus the clock and the fault
         * key. They are exposed because the round trip described above is
         * only worth claiming if something checks it: a host test drives
         * microvolts_at() at chosen instants, runs core/sensors forwards
         * over the result, and asserts it lands back on what the
         * scripted_* functions say was there. Reading the wall clock
         * instead would make that assertion depend on when the test ran.
         *---------------------------------------------------------------*/

        /**
         * @param ch            Channel index.
         * @param t_ms          Milliseconds since boot; wraps at the cycle
         *                      length, so any value is valid.
         * @param fault_channel Channel faking an open circuit, or -1.
         * @param out           Voltage at the ADC pin, microvolts.
         */
        static bool microvolts_at(uint8_t   ch,
                                  uint32_t  t_ms,
                                  int8_t    fault_channel,
                                  uint32_t &out);

        /**
         * Ground truth: what the scripted engine is actually doing at
         * `t_ms`, in the units a gauge would display. */
        static float scripted_oil_pressure_psi(uint32_t t_ms);
        static float scripted_oil_temp_c(uint32_t t_ms);
        static float scripted_coolant_temp_c(uint32_t t_ms);
        static float scripted_rail_volts(uint32_t t_ms);

    private:
        /**
         * Which channel is faking an open circuit, or -1 for none. Cycled
         * by the 'f' key so the plausibility band that core/sensors will
         * grow (and the blanked panel it should produce) is reachable on
         * demand rather than needing a wire pulled. */
        int8_t fault_channel_ = -1;

        void service_fault_key();
};
