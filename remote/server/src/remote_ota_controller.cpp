#include "remote_ota_controller.hpp"

#include <string.h>

namespace remote::server
{
    using protocol::MsgType;
    using protocol::OtaResult;

    FLASHMEM RemoteOtaController::RemoteOtaController(RemoteConfigLink &link)
        : link_(link)
    {
    }

    FLASHMEM void RemoteOtaController::on_event(MsgType        type,
                                                const uint8_t *payload,
                                                uint16_t       len,
                                                uint32_t       now_ms)
    {
        switch (type)
        {
            case MsgType::EVT_OTA_REQUEST:
                if (state_ == OtaState::Idle)
                {
                    enter(OtaState::Requested, now_ms);
                }
                else if (state_ == OtaState::Armed)
                {
                    /* Already agreed to -- a reloaded page asking again.
                     * Say yes again rather than leaving it waiting. */
                    send(MsgType::CMD_OTA_ARM);
                }
                break;

            case MsgType::EVT_OTA_BEGIN: {
                /* The relay only takes uploads in update mode, so outside
                 * it this can't be ours to show. A new upload supersedes
                 * one waiting at Ready -- it overwrites the same slot. */
                if (len < protocol::OTA_BEGIN_BYTES
                    || (state_ != OtaState::Armed && state_ != OtaState::Ready
                        && state_ != OtaState::Receiving))
                {
                    return;
                }

                protocol::OtaBeginWire wire {};
                protocol::decode_ota_begin(payload, wire);
                memcpy(version_, wire.version, sizeof(version_));
                total_            = wire.total_bytes;
                written_          = 0;
                last_progress_ms_ = now_ms;
                last_result_      = OtaResult::Ok;
                enter(OtaState::Receiving, now_ms);
                break;
            }

            case MsgType::EVT_OTA_PROGRESS:
                if (state_ != OtaState::Receiving || len < 4)
                {
                    return;
                }
                protocol::get_u32(payload, written_);
                last_progress_ms_ = now_ms;
                break;

            case MsgType::EVT_OTA_RESULT: {
                if (len < 1)
                {
                    return;
                }
                uint8_t raw = 0;
                protocol::get_u8(payload, raw);
                auto result = static_cast<OtaResult>(raw);

                if (state_ == OtaState::Receiving && result == OtaResult::Ok)
                {
                    written_     = total_;
                    last_result_ = result;
                    enter(OtaState::Ready, now_ms);
                }
                else if (state_ == OtaState::Receiving
                         || state_ == OtaState::Installing)
                {
                    /* A failed upload, or a commit the relay couldn't
                     * carry out -- either way the running firmware stays,
                     * and the phone can send again. */
                    fail(result, now_ms);
                }
                break;
            }

            default:
                break;
        }
    }

    FLASHMEM void RemoteOtaController::tick(uint32_t now_ms)
    {
        switch (state_)
        {
            case OtaState::Requested:
                if (now_ms - state_entered_ms_ > OTA_REQUEST_TIMEOUT_MS)
                {
                    decline();
                }
                break;

            case OtaState::Armed:
                if (now_ms - state_entered_ms_ > OTA_WAIT_TIMEOUT_MS)
                {
                    leave();
                }
                break;

            case OtaState::Receiving:
                if (now_ms - last_progress_ms_ > OTA_STALL_TIMEOUT_MS)
                {
                    fail(OtaResult::ErrInterrupted, now_ms);
                }
                break;

            case OtaState::Installing:
                if (now_ms - state_entered_ms_ > OTA_INSTALL_TIMEOUT_MS)
                {
                    fail(OtaResult::ErrFlash, now_ms);
                }
                break;

            case OtaState::Idle:
            case OtaState::Ready:
            default:
                break;
        }
    }

    FLASHMEM void RemoteOtaController::accept(uint32_t now_ms)
    {
        if (state_ != OtaState::Requested)
        {
            return;
        }
        send(MsgType::CMD_OTA_ARM);
        last_result_ = OtaResult::Ok;
        enter(OtaState::Armed, now_ms);
    }

    FLASHMEM void RemoteOtaController::decline()
    {
        if (state_ != OtaState::Requested)
        {
            return;
        }
        send(MsgType::CMD_OTA_DISARM);
        state_ = OtaState::Idle;
    }

    FLASHMEM void RemoteOtaController::leave()
    {
        if (state_ != OtaState::Armed && state_ != OtaState::Ready)
        {
            return;
        }
        send(MsgType::CMD_OTA_DISARM);
        state_ = OtaState::Idle;
    }

    FLASHMEM void RemoteOtaController::install(uint32_t now_ms)
    {
        if (state_ != OtaState::Ready)
        {
            return;
        }
        send(MsgType::CMD_OTA_COMMIT);
        enter(OtaState::Installing, now_ms);
    }

    FLASHMEM uint8_t RemoteOtaController::percent() const
    {
        if (total_ == 0)
        {
            return 0;
        }
        uint32_t written = written_ > total_ ? total_ : written_;
        return static_cast<uint8_t>(static_cast<uint64_t>(written) * 100u
                                    / total_);
    }

    FLASHMEM void RemoteOtaController::enter(OtaState state, uint32_t now_ms)
    {
        state_            = state;
        state_entered_ms_ = now_ms;
    }

    FLASHMEM void RemoteOtaController::fail(OtaResult result, uint32_t now_ms)
    {
        last_result_ = result;
        enter(OtaState::Armed, now_ms);
    }

    FLASHMEM void RemoteOtaController::send(MsgType type)
    {
        link_.send_command(type, nullptr, 0);
    }

} // namespace remote::server
