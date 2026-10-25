#include "dash_s3_ota.hpp"

#include <Arduino.h>
#include <string.h>

#include "web_update_gz.h" /* generated -- see tools/web/embed_web.py */

using remote::protocol::MsgType;
using remote::protocol::OtaResult;
namespace protocol = remote::protocol;

namespace
{
    constexpr const char *UPDATE_URI = "/update";

    /* EVT_OTA_PROGRESS granularity: ~20 reports across a 1.2MB image,
     * plenty for a progress bar, and each one a frame through the pipe to
     * the dash rather than a flood of them. */
    constexpr uint32_t PROGRESS_STEP_BYTES = 64u * 1024u;

    static_assert(protocol::WIRE_VERSION_LEN == FIRMWARE_VERSION_LEN,
                  "EVT_OTA_BEGIN's version field must fit a firmware id's");

    /* What the phone is told, by result. */
    struct Reply
    {
            int         code;
            const char *text;
    };

    Reply reply_for(OtaResult result)
    {
        switch (result)
        {
            case OtaResult::ErrNotFirmware:
                return { 415, "That isn't a firmware file for this dash." };
            case OtaResult::ErrWrongHardware:
                return { 409, "That firmware is built for different dash "
                              "hardware." };
            case OtaResult::ErrTooLarge:
                return { 413, "That file is too big to be dash firmware." };
            case OtaResult::ErrCorrupt:
                return { 422, "The file is damaged or incomplete." };
            case OtaResult::ErrNotArmed:
                return { 403, "Start update mode on the dash first." };
            case OtaResult::ErrFlash:
            case OtaResult::ErrInterrupted:
            default:
                return { 500, "The dash couldn't save the update. Try "
                              "again." };
        }
    }
} // namespace

void DashS3Updater::attach(WebServer &server, esp32::RelayLink &link)
{
    server_ = &server;
    link_   = &link;

    server.on(UPDATE_URI, HTTP_GET, [this]() { handle_page(); });
    server.on("/api/version", HTTP_GET, [this]() { handle_version(); });
    server.on("/api/update/request", HTTP_POST, [this]() {
        handle_mode_request();
    });
    server.on("/api/update/status", HTTP_GET, [this]() {
        handle_mode_status();
    });
    server.addHandler(this);
}

void DashS3Updater::on_frame(MsgType type, const uint8_t *payload, uint16_t len)
{
    (void)payload;
    (void)len;
    switch (type)
    {
        case MsgType::CMD_OTA_ARM:
            mode_ = Mode::Armed;
            break;

        case MsgType::CMD_OTA_DISARM:
            mode_ = mode_ == Mode::Pending ? Mode::Declined : Mode::Off;
            /* Out of update mode, nothing is installable -- even an image
             * that was waiting on Install. */
            ready_slot_ = nullptr;
            break;

        case MsgType::CMD_OTA_COMMIT:
            commit();
            break;

        default:
            break;
    }
}

void DashS3Updater::handle_page()
{
    /* no-store for the same reason as the config page (config_web.cpp):
     * a phone must never keep running an old copy against new firmware. */
    server_->sendHeader("Cache-Control", "no-store");
    server_->sendHeader("Content-Encoding", "gzip");
    server_->send_P(200,
                    "text/html",
                    reinterpret_cast<PGM_P>(WEB_UPDATE_GZ),
                    WEB_UPDATE_GZ_LEN);
}

void DashS3Updater::handle_version()
{
    server_->sendHeader("Cache-Control", "no-store");
    server_->send(200, "text/plain", platform_firmware_version());
}

/* Answers at once rather than waiting on the panel: the person might take
 * half a minute to tap, and this server serves one request at a time. The
 * page polls /api/update/status for the answer. */
void DashS3Updater::handle_mode_request()
{
    if (mode_ != Mode::Armed)
    {
        mode_ = Mode::Pending;
        link_->send_event(MsgType::EVT_OTA_REQUEST, nullptr, 0);
    }
    handle_mode_status();
}

void DashS3Updater::handle_mode_status()
{
    const char *text = "off";
    switch (mode_)
    {
        case Mode::Pending:
            text = "pending";
            break;
        case Mode::Declined:
            text = "declined";
            break;
        case Mode::Armed:
            text = "armed";
            break;
        case Mode::Off:
        default:
            break;
    }
    server_->sendHeader("Cache-Control", "no-store");
    server_->send(200, "text/plain", text);
}

/*==============================================================================
 * POST /update
 =============================================================================*/

bool DashS3Updater::canHandle(HTTPMethod method, String uri)
{
    return method == HTTP_POST && uri == UPDATE_URI;
}

bool DashS3Updater::canRaw(String uri)
{
    return uri == UPDATE_URI;
}

void DashS3Updater::raw(WebServer &server, String uri, HTTPRaw &raw)
{
    (void)uri;
    switch (raw.status)
    {
        case RAW_START:
            start_upload(static_cast<size_t>(server.clientContentLength()));
            break;
        case RAW_WRITE:
            take_bytes(raw.buf, raw.currentSize);
            break;
        case RAW_END:
            end_upload();
            break;
        case RAW_ABORTED:
            abort_upload();
            break;
    }
}

bool DashS3Updater::handle(WebServer &server, HTTPMethod method, String uri)
{
    if (!canHandle(method, uri))
    {
        return false;
    }

    /* Only reached once the body has been read in full (an aborted one
     * never gets an answer -- the phone has gone). No raw body at all
     * means WebServer took it as a form, which the page never sends. */
    if (!body_seen_)
    {
        server.send(400, "text/plain", "Send the .bin as the request body.");
        return true;
    }
    body_seen_ = false;

    if (result_ == OtaResult::Ok)
    {
        server.send(200, "text/plain", version_);
    }
    else
    {
        Reply reply = reply_for(result_);
        server.send(reply.code, "text/plain", reply.text);
    }
    return true;
}

void DashS3Updater::start_upload(size_t content_length)
{
    body_seen_ = true;
    ota_begun_ = false;
    result_    = OtaResult::Ok;
    total_     = static_cast<uint32_t>(content_length);
    written_   = 0;
    reported_  = 0;
    head_len_  = 0;
    version_[0] = '\0';
    /* Whatever was waiting on a commit is about to be overwritten. */
    ready_slot_ = nullptr;

    /* WebServer reads a raw body with Stream::readBytes(), which spins
     * without yielding while it waits for data -- for up to the client's
     * 5s timeout if the phone stalls. That would starve core 0's idle
     * task right up against the task watchdog's own 5s and reset the
     * board mid-upload. Its watch is lifted for the upload's duration
     * instead; the Wi-Fi stack's tasks outrank this one and are
     * unaffected. */
    if (!idle_watchdog_off_)
    {
        disableCore0WDT();
        idle_watchdog_off_ = true;
    }

    if (mode_ != Mode::Armed)
    {
        fail(OtaResult::ErrNotArmed);
        return;
    }

    slot_ = esp_ota_get_next_update_partition(nullptr);
    if (slot_ == nullptr)
    {
        fail(OtaResult::ErrFlash);
        return;
    }
    if (content_length > slot_->size)
    {
        fail(OtaResult::ErrTooLarge);
    }
}

void DashS3Updater::take_bytes(const uint8_t *data, size_t len)
{
    if (result_ != OtaResult::Ok)
    {
        return; /* failed already -- drain the rest unread */
    }

    if (!ota_begun_)
    {
        size_t n = FIRMWARE_ID_END - head_len_;
        n        = n < len ? n : len;
        memcpy(head_ + head_len_, data, n);
        head_len_ += n;
        data += n;
        len -= n;
        if (head_len_ < FIRMWARE_ID_END)
        {
            return;
        }

        switch (dash_s3_check_image(head_, head_len_, version_))
        {
            case FirmwareImageCheck::Ok:
                break;
            case FirmwareImageCheck::WrongHardware:
                fail(OtaResult::ErrWrongHardware);
                return;
            case FirmwareImageCheck::NotFirmware:
            default:
                fail(OtaResult::ErrNotFirmware);
                return;
        }

        /* Sequential writes: each sector is erased as the image reaches
         * it, rather than the whole 2MB slot up front -- seconds of
         * blocking erase before the first byte went down. */
        if (esp_ota_begin(slot_, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle_)
            != ESP_OK)
        {
            fail(OtaResult::ErrFlash);
            return;
        }
        ota_begun_ = true;

        protocol::OtaBeginWire begin {};
        begin.total_bytes = total_;
        memcpy(begin.version, version_, sizeof(begin.version));
        uint8_t payload[protocol::OTA_BEGIN_BYTES];
        protocol::encode_ota_begin(payload, begin);
        link_->send_event(MsgType::EVT_OTA_BEGIN, payload, sizeof(payload));

        if (!write_image(head_, head_len_))
        {
            return;
        }
    }

    write_image(data, len);
}

bool DashS3Updater::write_image(const uint8_t *data, size_t len)
{
    if (len == 0)
    {
        return true;
    }
    if (esp_ota_write(ota_handle_, data, len) != ESP_OK)
    {
        fail(OtaResult::ErrFlash);
        return false;
    }

    written_ += static_cast<uint32_t>(len);
    if (written_ - reported_ >= PROGRESS_STEP_BYTES)
    {
        reported_ = written_;
        uint8_t payload[4];
        protocol::put_u32(payload, written_);
        link_->send_event(MsgType::EVT_OTA_PROGRESS, payload, sizeof(payload));
    }
    return true;
}

void DashS3Updater::end_upload()
{
    if (result_ == OtaResult::Ok && !ota_begun_)
    {
        /* Over before there was enough of it to identify. */
        result_ = OtaResult::ErrNotFirmware;
    }
    else if (result_ == OtaResult::Ok)
    {
        /* Checks the image's checksum and appended SHA-256 -- a truncated
         * or corrupted upload stops here. Frees the handle either way. */
        esp_err_t err = esp_ota_end(ota_handle_);
        ota_begun_    = false;
        if (err == ESP_OK)
        {
            ready_slot_ = slot_;
            memcpy(ready_version_, version_, sizeof(ready_version_));
            send_result(OtaResult::Ok);
        }
        else
        {
            result_ = err == ESP_ERR_OTA_VALIDATE_FAILED ? OtaResult::ErrCorrupt
                                                         : OtaResult::ErrFlash;
            send_result(result_);
        }
    }
    restore_idle_watchdog();
}

void DashS3Updater::abort_upload()
{
    fail(OtaResult::ErrInterrupted);
    body_seen_ = false; /* no response goes out for this one */
    restore_idle_watchdog();
}

void DashS3Updater::fail(OtaResult result)
{
    if (result_ != OtaResult::Ok)
    {
        return; /* the first failure is the one reported */
    }
    result_ = result;

    /* The panel only hears of uploads that got past identification, so
     * only those get a result. */
    if (ota_begun_)
    {
        esp_ota_abort(ota_handle_);
        ota_begun_ = false;
        send_result(result);
    }
}

void DashS3Updater::send_result(OtaResult result)
{
    uint8_t payload = static_cast<uint8_t>(result);
    link_->send_event(MsgType::EVT_OTA_RESULT, &payload, 1);
}

void DashS3Updater::restore_idle_watchdog()
{
    if (idle_watchdog_off_)
    {
        enableCore0WDT();
        idle_watchdog_off_ = false;
    }
}

/*==============================================================================
 * CMD_OTA_COMMIT
 =============================================================================*/

void DashS3Updater::commit()
{
    if (ready_slot_ == nullptr || mode_ != Mode::Armed)
    {
        /* Nothing verified to boot -- the panel's Ready was stale. */
        send_result(OtaResult::ErrFlash);
        return;
    }

    /* Re-verifies the whole image before writing otadata. The new image
     * then boots on probation (see dash_s3_firmware.cpp), and the
     * bootloader falls back to this one if it never proves itself. */
    esp_err_t err = esp_ota_set_boot_partition(ready_slot_);
    if (err != ESP_OK)
    {
        ready_slot_ = nullptr;
        send_result(err == ESP_ERR_OTA_VALIDATE_FAILED ? OtaResult::ErrCorrupt
                                                       : OtaResult::ErrFlash);
        return;
    }

    dash_s3_note_install(ready_slot_->address, ready_version_);
    esp_restart();
}
