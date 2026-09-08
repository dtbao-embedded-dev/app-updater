/**
 * @file    nvs_fake.h
 * @date    2026-09-07
 * @brief   Test-side control surface for the host fake of ESP-IDF NVS.
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

#ifndef HOST_FAKE_NVS_FAKE_H
#define HOST_FAKE_NVS_FAKE_H

/* ------------------------------ Includes ------------------------------- */

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------- Constants & macros ------------------------- */

/** One key's worth of storage, generous next to STORAGE_BLOB_MAX. */
#define NVS_FAKE_CAP 512U

/* ------------------------ Public function prototypes ------------------- */

/*
 * One namespace, one key, one blob - which is the whole of NVS that
 * `driver/storage` uses. Anything more would be faking features no caller can
 * reach.
 *
 * Every test calls nvs_fake_reset() first (R-TST-05).
 */

/** @brief Empties the store and clears every forced failure and counter. */
void nvs_fake_reset(void);

/** @brief Preloads the stored blob, as a previous boot would have left it. */
void nvs_fake_preload(const void *data, size_t len);

/** @brief Bytes currently stored, or 0 when the key has never been written. */
size_t nvs_fake_stored_len(void);

/** @brief The stored bytes; valid until the next write or reset. */
const uint8_t *nvs_fake_stored(void);

/**
 * @brief   How many times `nvs_set_blob()` actually wrote.
 * @note    **This is the wear-guard assertion.** A save of bytes that already
 *          match must leave this unchanged; a sector erase per identical save
 *          is how a settings record wears a partition out (R-CFG-05).
 */
uint32_t nvs_fake_write_count(void);

/** @brief How many times `nvs_commit()` was called. */
uint32_t nvs_fake_commit_count(void);

/** @brief How many times `nvs_flash_erase()` was called. */
uint32_t nvs_fake_erase_count(void);

/** @brief True while a handle opened by `nvs_open()` has not been closed. */
bool nvs_fake_is_open(void);

/** @brief Forces the matching call to fail; ESP_OK clears it. */
void nvs_fake_fail_flash_init(esp_err_t err);
void nvs_fake_fail_open(esp_err_t err);
void nvs_fake_fail_get(esp_err_t err);
void nvs_fake_fail_set(esp_err_t err);
void nvs_fake_fail_commit(esp_err_t err);

#ifdef __cplusplus
}
#endif

#endif /* HOST_FAKE_NVS_FAKE_H */

/*** end of file ***/
