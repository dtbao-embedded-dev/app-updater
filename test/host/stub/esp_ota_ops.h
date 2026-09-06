/**
 * @file    esp_ota_ops.h
 * @date    2026-09-06
 * @brief   Host fake for the ESP-IDF OTA operations header (R-TST-07).
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

#ifndef HOST_STUB_ESP_OTA_OPS_H
#define HOST_STUB_ESP_OTA_OPS_H

/* ------------------------------ Includes ------------------------------- */

#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_partition.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------- Constants & macros ------------------------- */

/** Tells esp_ota_begin() to work out the erase size from the image itself. */
#define OTA_SIZE_UNKNOWN 0xFFFFFFFFU

/* -------------------------------- Types -------------------------------- */

/* A uint32_t on target too, which is why command_t can carry it without
 * dragging the SDK type into a public header (R-LAY-03). */
typedef uint32_t esp_ota_handle_t;

/* ------------------------ Public function prototypes ------------------- */

/** @brief The slot the fake was told is running. Never NULL. */
const esp_partition_t *esp_ota_get_running_partition(void);

/**
 * @brief   Reads the image header of a slot.
 * @return  ESP_OK, or ESP_ERR_NOT_FOUND when the test said that slot holds no
 *          valid image - which is the case the VERSION command has to answer
 *          with sixteen zero bytes rather than a failure.
 */
esp_err_t esp_ota_get_partition_description(const esp_partition_t *partition,
                                            esp_app_desc_t *out_desc);

/**
 * @brief   Arms a slot for the next boot.
 * @return  ESP_OK, or whatever `esp_fake_fail_set_boot()` was told to return.
 *          The slot armed is recorded for `esp_fake_boot_slot_set()`.
 */
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *partition);

/**
 * @brief   Opens a write session, erasing the target slot.
 * @note    The erase is the expensive, irreversible part, which is why every
 *          UPG_BEGIN argument check has to happen before this call. The fake
 *          counts the calls so a test can prove a rejected request erased
 *          nothing.
 */
esp_err_t esp_ota_begin(const esp_partition_t *partition, size_t image_size,
                        esp_ota_handle_t *out_handle);

/** @brief Appends bytes to an open session. */
esp_err_t esp_ota_write(esp_ota_handle_t handle, const void *data, size_t size);

/** @brief Validates the written image and marks the slot valid. Arms nothing. */
esp_err_t esp_ota_end(esp_ota_handle_t handle);

/** @brief Frees a session without finalising the slot. */
esp_err_t esp_ota_abort(esp_ota_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* HOST_STUB_ESP_OTA_OPS_H */

/*** end of file ***/
