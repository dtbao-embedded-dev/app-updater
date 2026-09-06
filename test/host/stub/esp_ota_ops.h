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

#ifdef __cplusplus
}
#endif

#endif /* HOST_STUB_ESP_OTA_OPS_H */

/*** end of file ***/
