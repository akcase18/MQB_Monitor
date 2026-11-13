/**
 * @file dash_s3_can.cpp
 * @brief TWAI wiring behind ICanBus. Response filtering by CAN ID is done
 * in core/can as well (UdsClient::poll drops anything but the modules'
 * response ids), but the hardware acceptance filter narrows to their range
 * too -- see begin() for why.
 * This layer only moves frames.
 */

#include "dash_s3_can.hpp"

#include <Arduino.h>
#include <string.h>

#include "dash_s3_pins.hpp"
#include "driver/twai.h"

namespace
{
    /**
     * Deeper than the driver's default of 5 in both directions.
     *
     * RX: receive() is called from loop() alongside lv_timer_handler(), so
     * the queue has to hold whatever arrives during a full LVGL repaint
     * (tens of milliseconds). With the hardware filter narrowing that to
     * frames on rx_id it is only ever a UDS response and its 0x78
     * "response pending" predecessors.
     *
     * TX: core::SignalPoller keeps one request outstanding at a time.
     */
    constexpr uint32_t RX_QUEUE_LEN = 32;
    constexpr uint32_t TX_QUEUE_LEN = 8;

    /**
     * Both payload arrays are 8 bytes (CanFrame::data, twai_message_t::data),
     * so this only ever clamps a frame claiming a length longer than a
     * classic CAN frame can hold.
     */
    uint8_t clamped_len(uint8_t len)
    {
        return len > 8 ? 8 : len;
    }

    bool timing_for(uint32_t baud, twai_timing_config_t &t)
    {
        switch (baud)
        {
            case 1000000:
                t = TWAI_TIMING_CONFIG_1MBITS();
                return true;
            case 500000:
                t = TWAI_TIMING_CONFIG_500KBITS();
                return true;
            case 250000:
                t = TWAI_TIMING_CONFIG_250KBITS();
                return true;
            case 125000:
                t = TWAI_TIMING_CONFIG_125KBITS();
                return true;
            default:
                return false;
        }
    }

    /**
     * Bus-off happens immediately on a bench with no transceiver (every
     * transmit errors out) and can happen on the car with a bad splice.
     * The driver won't recover on its own: once the controller is
     * bus-off, twai_initiate_recovery() has to be called, and after the
     * recovery completes the driver drops back to the stopped state and
     * needs twai_start() again. Doing that from receive() means the bus heals
     * itself once the wiring is fixed without a reboot.
     */
    void service_bus_state()
    {
        twai_status_info_t status;
        if (twai_get_status_info(&status) != ESP_OK)
        {
            return;
        }

        switch (status.state)
        {
            case TWAI_STATE_BUS_OFF:
                Serial.println("TWAI: bus-off, initiating recovery");
                twai_initiate_recovery();
                break;
            case TWAI_STATE_STOPPED:
                Serial.println("TWAI: recovered, restarting");
                twai_start();
                break;
            default:
                break;
        }
    }
} // namespace

DashS3CanBus &DashS3CanBus::instance()
{
    static DashS3CanBus s_instance;
    return s_instance;
}

bool DashS3CanBus::begin(uint32_t rx_id_lo, uint32_t rx_id_hi, uint32_t baud)
{
    twai_timing_config_t t_cfg;
    if (!timing_for(baud, t_cfg))
    {
        Serial.printf("TWAI: unsupported baud %lu\n",
                      static_cast<unsigned long>(baud));
        return false;
    }

    twai_general_config_t g_cfg = {};
    g_cfg.mode                  = TWAI_MODE_NORMAL;
    g_cfg.tx_io                 = static_cast<gpio_num_t>(DASH_S3_PIN_CAN_TX);
    g_cfg.rx_io                 = static_cast<gpio_num_t>(DASH_S3_PIN_CAN_RX);
    g_cfg.clkout_io             = TWAI_IO_UNUSED;
    g_cfg.bus_off_io            = TWAI_IO_UNUSED;
    g_cfg.tx_queue_len          = TX_QUEUE_LEN;
    g_cfg.rx_queue_len          = RX_QUEUE_LEN;
    g_cfg.alerts_enabled        = TWAI_ALERT_NONE;
    g_cfg.clkout_divider        = 0;
    g_cfg.intr_flags            = ESP_INTR_FLAG_LEVEL1;

    /**
     * First-pass hardware filter to ignore most of the extra CAN IDs. Can't
     * catch all of them, but the rest are filtered in core/can.
     */
    uint32_t span = 0;
    for (uint32_t diff = rx_id_lo ^ rx_id_hi; diff != 0; diff >>= 1)
    {
        span = (span << 1) | 1u;
    }
    twai_filter_config_t f_cfg = {};
    f_cfg.acceptance_code      = (rx_id_lo & ~span) << 21;
    f_cfg.acceptance_mask      = (span << 21) | 0x001FFFFF;
    f_cfg.single_filter        = true;

    esp_err_t err = twai_driver_install(&g_cfg, &t_cfg, &f_cfg);
    if (err != ESP_OK)
    {
        Serial.printf("TWAI: driver install failed: %s\n",
                      esp_err_to_name(err));
        return false;
    }

    err = twai_start();
    if (err != ESP_OK)
    {
        Serial.printf("TWAI: start failed: %s\n", esp_err_to_name(err));
        return false;
    }

    started_ = true;
    Serial.println("TWAI started");
    return true;
}

bool DashS3CanBus::send(const CanFrame &frame)
{
    if (!started_)
    {
        return false;
    }

    twai_message_t msg   = {};
    msg.identifier       = frame.id;
    msg.extd             = frame.id > 0x7FF ? 1 : 0;
    msg.data_length_code = clamped_len(frame.len);
    memcpy(msg.data, frame.data, msg.data_length_code);

    /**
     * Zero ticks: non-blocking, fails if the TX queue is full.
     */
    return twai_transmit(&msg, 0) == ESP_OK;
}

bool DashS3CanBus::receive(CanFrame &out)
{
    if (!started_)
    {
        return false;
    }

    service_bus_state();

    twai_message_t msg;
    if (twai_receive(&msg, 0) != ESP_OK)
    {
        return false;
    }
    if (msg.rtr)
    {
        /**
         * RTR messages are not supported
         */
        return false;
    }

    out.id  = msg.identifier;
    out.len = clamped_len(msg.data_length_code);
    memcpy(out.data, msg.data, out.len);

    return true;
}

CanBusStats DashS3CanBus::bus_stats() const
{
    CanBusStats out;
    twai_status_info_t status;
    if (!started_ || twai_get_status_info(&status) != ESP_OK)
    {
        return out;
    }

    out.supported  = true;
    out.rx_missed  = status.rx_missed_count;
    out.rx_overrun = status.rx_overrun_count;
    out.bus_errors = status.bus_error_count;
    out.tx_failed  = status.tx_failed_count;
    return out;
}
