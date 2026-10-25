/**
 * @file dash_s3_wireless.cpp
 * @brief See dash_s3_wireless.hpp. Two stream buffers make the pipe:
 * `to_relay` (dash writes, relay reads) and `to_dash` (relay writes, dash
 * reads). A stream buffer wants exactly one reader task and one writer
 * task, and that is exactly the arrangement -- loopTask is the dash
 * side, the relay task the other -- so no further locking is needed.
 *
 * The relay task is the C3's loop() transplanted: RelayApp::poll() once
 * per millisecond, on the Wi-Fi core so the display never shares a core
 * with the HTTP handlers. RelayLink's blocking send_request_and_wait()
 * (a REQ_GET_STATE from a phone hitting GET /) spins inside that task
 * while loopTask's RemoteConfigLink::poll() answers it from the other
 * end of the pipe, which is the same shape the two boards had, with the
 * UART latency taken out.
 */

#include "dash_s3_wireless.hpp"

#include <Arduino.h>
#include <Stream.h>

#include "dash_s3_ota.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "relay_app.hpp"
#include "remote_protocol.hpp"

namespace
{
    /* One max-size frame each way, doubled: RESP_STATE (dash -> relay)
     * is the largest thing on the link at ~1.2KB, and only one request
     * is ever outstanding, so nothing ever queues behind a full frame. */
    constexpr size_t PIPE_BYTES
        = 2 * (remote::protocol::FRAME_MAX_PAYLOAD + 8);

    /* A write blocks this long for space before giving up short. In
     * practice the other side drains every millisecond and the buffer
     * holds two whole frames, so this only matters if the relay task
     * has died; remote::server's write_frame ignores short writes, and
     * the FrameReader on the far side resyncs on the next FRAME_SYNC. */
    constexpr TickType_t WRITE_TIMEOUT = pdMS_TO_TICKS(250);

    /* The C3 runs the same code on an 8KB loopTask; a little more here
     * because the S3 core's WebServer/String paths are the same and there
     * is RAM to spare. Priority 1 matches loopTask; core 0 is where
     * Wi-Fi already lives. */
    constexpr uint32_t    RELAY_TASK_STACK = 10240;
    constexpr UBaseType_t RELAY_TASK_PRIO  = 1;
    constexpr BaseType_t  RELAY_TASK_CORE  = 0;

    StreamBufferHandle_t s_to_relay = nullptr;
    StreamBufferHandle_t s_to_dash  = nullptr;

    /* The relay's end of the pipe, in the shape RelayLink wants. peek()
     * is implemented for Stream's contract though RelayLink never calls
     * it. */
    class PipeStream : public Stream
    {
        public:
            using Print::write;

            int available() override
            {
                return (peeked_ >= 0 ? 1 : 0)
                       + static_cast<int>(
                           xStreamBufferBytesAvailable(s_to_relay));
            }

            int read() override
            {
                if (peeked_ >= 0)
                {
                    int b   = peeked_;
                    peeked_ = -1;
                    return b;
                }
                uint8_t b;
                return xStreamBufferReceive(s_to_relay, &b, 1, 0) == 1 ? b
                                                                        : -1;
            }

            int peek() override
            {
                if (peeked_ < 0)
                {
                    peeked_ = read();
                }
                return peeked_;
            }

            size_t write(uint8_t b) override
            {
                return write(&b, 1);
            }

            size_t write(const uint8_t *buf, size_t len) override
            {
                return xStreamBufferSend(s_to_dash, buf, len, WRITE_TIMEOUT);
            }

        private:
            int peeked_ = -1;
    };

    void relay_task(void *)
    {
        static PipeStream      pipe;
        /* The C3 relay plus the dash's firmware updater (dash_s3_ota.hpp),
         * which the C3 never had. */
        static DashS3Updater   updater;
        static esp32::RelayApp app(pipe, &updater);
        app.begin();
        for (;;)
        {
            app.poll(millis());
            vTaskDelay(1);
        }
    }
} // namespace

DashS3Wireless &DashS3Wireless::instance()
{
    static DashS3Wireless s_instance;
    return s_instance;
}

void DashS3Wireless::begin(uint32_t baud)
{
    (void)baud;
    if (s_to_relay != nullptr)
    {
        return;
    }

    /* Trigger level 1: a reader blocked on the buffer wakes on the first
     * byte. Neither side blocks on read today, but it costs nothing. */
    s_to_relay = xStreamBufferCreate(PIPE_BYTES, 1);
    s_to_dash  = xStreamBufferCreate(PIPE_BYTES, 1);
    if (s_to_relay == nullptr || s_to_dash == nullptr)
    {
        Serial.println("Wireless: pipe allocation failed; relay not started");
        return;
    }

    xTaskCreatePinnedToCore(relay_task,
                            "relay",
                            RELAY_TASK_STACK,
                            nullptr,
                            RELAY_TASK_PRIO,
                            nullptr,
                            RELAY_TASK_CORE);
}

size_t DashS3Wireless::read(uint8_t *buf, size_t len)
{
    if (s_to_dash == nullptr)
    {
        return 0;
    }
    return xStreamBufferReceive(s_to_dash, buf, len, 0);
}

size_t DashS3Wireless::write(const uint8_t *buf, size_t len)
{
    if (s_to_relay == nullptr)
    {
        return 0;
    }
    return xStreamBufferSend(s_to_relay, buf, len, WRITE_TIMEOUT);
}
