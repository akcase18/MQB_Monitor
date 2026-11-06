/**
 * @file dash_s3_firmware.hpp
 * @brief Which firmware this is, and the board's side of OTA rollback.
 * See OTA_PLAN.md for the whole update flow.
 *
 * Every dash image carries a DashS3FirmwareId in `.rodata_custom_desc`,
 * a section ESP-IDF's linker script reserves for exactly this: it lands
 * straight after esp_app_desc_t at the head of the image's first segment,
 * byte FIRMWARE_ID_OFFSET of the .bin. So the first upload chunk is
 * enough to identify an image, and the updater can turn away the C3
 * relay's firmware.bin, another project's, or a dash build for other
 * hardware, or one that needs a partition table this board doesn't
 * have, before writing a byte of it. esp_app_desc_t's own
 * project_name can't do this job -- under Arduino it names the
 * precompiled SDK ("arduino-lib-builder"), whatever the app.
 *
 * The phone's update page reads the same bytes to show what it's about
 * to send (platform/dash_s3/web/update.html); keep the two in step.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "platform.hpp"

/**
 * Which dash hardware a build is for. An image only installs over one
 * built for the same variant: the dev kit's pinout and PSRAM mode don't
 * boot on the PCB, nor the other way round.
 */
#ifndef DASH_S3_HW_VARIANT
    #define DASH_S3_HW_VARIANT "pcb"
#endif

/**
 * What goes in the descriptor's variant field: the hardware, marked
 * "-stress" for a bench build that fakes its CAN data. The marking is
 * automatic so no stress build can pass for real firmware -- a stress
 * image is refused by a dash running the real thing, and the other way
 * round.
 */
#if DASH_S3_FAKE_CAN || STRESS_TEST
    #define DASH_S3_BUILD_VARIANT DASH_S3_HW_VARIANT "-stress"
#else
    #define DASH_S3_BUILD_VARIANT DASH_S3_HW_VARIANT
#endif

struct DashS3FirmwareId
{
        char magic[16];      /* DASH_S3_FIRMWARE_MAGIC */
        char hw_variant[16]; /* DASH_S3_BUILD_VARIANT */
        char version[FIRMWARE_VERSION_LEN];
        /**
         * Added after the first OTA-capable release, so an older image
         * has whatever its linker put next instead; the tag is how the
         * check tells. Untagged images all predate any change to the
         * table, so they count as LEGACY_PARTITION_LAYOUT.
         */
        char     layout_tag[4];    /* DASH_S3_LAYOUT_TAG */
        uint32_t partition_layout; /* FW_PARTITION_LAYOUT, little-endian */
};

constexpr char DASH_S3_FIRMWARE_MAGIC[] = "MQB-DASH-S3";
constexpr char DASH_S3_LAYOUT_TAG[4]    = { 'P', 'T', 'L', '1' };
/**
 * tools/partition_table.py's layout_crc() of partitions_dash.csv as of
 * 7b61dd0, the table every untagged dash image was built for.
 */
constexpr uint32_t LEGACY_PARTITION_LAYOUT = 0x93e3eb34u;

/**
 * esp_image_header_t (24) + esp_image_segment_header_t (8) +
 * esp_app_desc_t (256) -- checked against the real structs in
 * dash_s3_firmware.cpp.
 */
constexpr size_t FIRMWARE_ID_OFFSET = 288;
/**
 * How much of an image has to arrive before it can be identified.
 */
constexpr size_t FIRMWARE_ID_END = FIRMWARE_ID_OFFSET
                                   + sizeof(DashS3FirmwareId);

/**
 * This build's own id.
 */
const DashS3FirmwareId &dash_s3_firmware_id();

enum class FirmwareImageCheck : uint8_t
{
    Ok,
    NotFirmware,   /* not an ESP32-S3 app image carrying a dash id */
    WrongHardware, /* a dash image for other hardware, or a stress build */
    NeedsUsb,      /* built for a different partition table */
};

/**
 * Identifies an uploaded image from its first FIRMWARE_ID_END bytes.
 * On Ok, copies its version into `version` (FIRMWARE_VERSION_LEN bytes,
 * always terminated).
 */
FirmwareImageCheck dash_s3_check_image(const uint8_t *head,
                                       size_t         len,
                                       char          *version);

/**
 * Records that the app slot at `slot_address` is about to be booted with
 * `version` in it -- call after switching slots, right before restarting.
 * The next boot compares it with where it actually came up, which is how
 * platform_take_failed_update() spots a rollback.
 */
void dash_s3_note_install(uint32_t slot_address, const char *version);
