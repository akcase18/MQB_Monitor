/**
 * @file dash_s3_firmware.cpp
 * @brief See dash_s3_firmware.hpp, and platform.hpp's FIRMWARE section,
 * which this implements for the dash.
 *
 * Rollback itself is the bootloader's: the SDK is built with
 * CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE, so a freshly installed image
 * boots as ESP_OTA_IMG_PENDING_VERIFY, and if anything resets the board
 * before the image marks itself valid, the bootloader boots the other
 * slot. All this file adds is keeping the Arduino core from marking every
 * image valid the moment it starts (verifyRollbackLater()), the mark-valid
 * call for main.cpp to make once the app has proven itself, and an NVS
 * note so the old firmware can say so when it finds itself running again.
 */

#include "dash_s3_firmware.hpp"

#include <Arduino.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "esp_app_format.h"
#include "esp_ota_ops.h"
#include "fw_version.h" /* generated -- see tools/fw_version.py */
#include "nvs.h"

static_assert(FIRMWARE_ID_OFFSET
                  == sizeof(esp_image_header_t)
                         + sizeof(esp_image_segment_header_t)
                         + sizeof(esp_app_desc_t),
              "the update descriptor is no longer where uploads look for it");
static_assert(sizeof(DASH_S3_HW_VARIANT)
                  <= sizeof(DashS3FirmwareId::hw_variant),
              "DASH_S3_HW_VARIANT is too long");

namespace
{
    /* Right behind esp_app_desc_t -- see the header. Referenced by
     * dash_s3_firmware_id(), which is what keeps --gc-sections from
     * dropping it (the linker script doesn't KEEP this section). */
    __attribute__((section(".rodata_custom_desc"), used))
    const DashS3FirmwareId s_firmware_id = {
        "MQB-DASH-S3",
        DASH_S3_HW_VARIANT,
        FW_VERSION,
    };

    /* The install note: which slot was meant to boot, with what. Its own
     * namespace, clear of the EEPROM emulation's (Esp32ConfigStore). */
    constexpr const char *NVS_NAMESPACE = "ota";
    constexpr const char *NVS_SLOT      = "slot";
    constexpr const char *NVS_VERSION   = "ver";

    bool s_loop_watchdog = false;

    /** Two of DashS3FirmwareId's fixed-width text fields hold the same
     * string. */
    bool same_text(const char *a, const char *b, size_t len)
    {
        return strncmp(a, b, len) == 0;
    }
} // namespace

/* Overrides the Arduino core's weak default (esp32-hal-misc.c), which
 * returns false and so has initArduino() mark every image valid before
 * setup() even runs -- leaving rollback nothing to roll back from. With
 * this, a new image stays on probation until platform_firmware_healthy().
 * C linkage, to match the C definition it replaces. */
extern "C" bool verifyRollbackLater()
{
    return true;
}

const DashS3FirmwareId &dash_s3_firmware_id()
{
    return s_firmware_id;
}

FirmwareImageCheck dash_s3_check_image(const uint8_t *head,
                                       size_t         len,
                                       char          *version)
{
    if (len < FIRMWARE_ID_END)
    {
        return FirmwareImageCheck::NotFirmware;
    }

    esp_image_header_t image;
    memcpy(&image, head, sizeof(image));
    if (image.magic != ESP_IMAGE_HEADER_MAGIC
        || image.chip_id != ESP_CHIP_ID_ESP32S3)
    {
        return FirmwareImageCheck::NotFirmware;
    }

    esp_app_desc_t app;
    memcpy(&app,
           head + sizeof(esp_image_header_t)
               + sizeof(esp_image_segment_header_t),
           sizeof(app));
    if (app.magic_word != ESP_APP_DESC_MAGIC_WORD)
    {
        return FirmwareImageCheck::NotFirmware;
    }

    DashS3FirmwareId id;
    memcpy(&id, head + FIRMWARE_ID_OFFSET, sizeof(id));
    if (!same_text(id.magic, s_firmware_id.magic, sizeof(id.magic)))
    {
        return FirmwareImageCheck::NotFirmware;
    }
    if (!same_text(
            id.hw_variant, s_firmware_id.hw_variant, sizeof(id.hw_variant)))
    {
        return FirmwareImageCheck::WrongHardware;
    }

    memcpy(version, id.version, FIRMWARE_VERSION_LEN);
    version[FIRMWARE_VERSION_LEN - 1] = '\0';
    return FirmwareImageCheck::Ok;
}

void dash_s3_note_install(uint32_t slot_address, const char *version)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK)
    {
        return;
    }
    nvs_set_u32(nvs, NVS_SLOT, slot_address);
    nvs_set_str(nvs, NVS_VERSION, version);
    nvs_commit(nvs);
    nvs_close(nvs);
}

const char *platform_firmware_version()
{
    return s_firmware_id.version;
}

void platform_firmware_healthy()
{
    static bool done = false;
    if (done)
    {
        return;
    }
    done = true;

    /* A no-op unless this image is the one on probation. */
    esp_ota_mark_app_valid_cancel_rollback();

    /* The update took, so the note has served its purpose. Cleared here,
     * not when it's first read at boot: an image that crashes after
     * reading it would otherwise take the old firmware's only clue to
     * the rollback down with it. */
    const esp_partition_t *running = esp_ota_get_running_partition();
    nvs_handle_t           nvs;
    uint32_t               slot = 0;
    if (running != nullptr
        && nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK)
    {
        if (nvs_get_u32(nvs, NVS_SLOT, &slot) == ESP_OK
            && slot == running->address)
        {
            nvs_erase_all(nvs);
            nvs_commit(nvs);
        }
        nvs_close(nvs);
    }
}

bool platform_take_failed_update(char *version, size_t cap)
{
    static bool taken = false;
    if (taken || cap == 0)
    {
        return false;
    }
    taken = true;

    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK)
    {
        return false;
    }

    uint32_t slot = 0;
    if (nvs_get_u32(nvs, NVS_SLOT, &slot) != ESP_OK)
    {
        nvs_close(nvs); /* no update pending -- the usual case */
        return false;
    }

    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running != nullptr && running->address == slot)
    {
        /* Up on the new image, which is still proving itself --
         * platform_firmware_healthy() clears the note once it has. */
        nvs_close(nvs);
        return false;
    }

    char   failed[FIRMWARE_VERSION_LEN] = "";
    size_t len                          = sizeof(failed);
    if (nvs_get_str(nvs, NVS_VERSION, failed, &len) != ESP_OK)
    {
        failed[0] = '\0';
    }
    nvs_erase_all(nvs);
    nvs_commit(nvs);
    nvs_close(nvs);

    snprintf(version, cap, "%s", failed);
    return true;
}

void platform_hang_watchdog(bool enabled)
{
    /* The Arduino core's loop watchdog: loopTask joins the task watchdog
     * (5s, and it panics -- i.e. resets -- per the SDK's sdkconfig) and
     * feeds it on every pass through loop(). Tracked here because
     * enabling it twice logs an error. */
    if (enabled == s_loop_watchdog)
    {
        return;
    }
    s_loop_watchdog = enabled;
    if (enabled)
    {
        enableLoopWDT();
    }
    else
    {
        disableLoopWDT();
    }
}
