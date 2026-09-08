/**
 * @file    nvs.h
 * @date    2026-09-07
 * @brief   Host stub for the ESP-IDF NVS header (R-TST-07).
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

#ifndef HOST_STUB_NVS_H
#define HOST_STUB_NVS_H

/* ------------------------------ Includes ------------------------------- */

#include "esp_err.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------- Constants & macros ------------------------- */

/* A real shadow of a vendor header, unlike the driver fakes in `fake/`: the
 * code under test here IS the driver, so there is nothing below it to fake and
 * the SDK boundary is the only seam available. The behaviour behind these
 * prototypes lives in fake/nvs_fake.c. */

/* The NVS codes `storage.c` distinguishes, at ESP-IDF's own values so a test
 * can state the vendor value it is faking and mean what the target would. */
#define ESP_ERR_NVS_BASE              0x1100
#define ESP_ERR_NVS_NOT_FOUND         (ESP_ERR_NVS_BASE + 0x02)
#define ESP_ERR_NVS_NOT_ENOUGH_SPACE  (ESP_ERR_NVS_BASE + 0x05)
#define ESP_ERR_NVS_INVALID_LENGTH    (ESP_ERR_NVS_BASE + 0x0c)
#define ESP_ERR_NVS_NO_FREE_PAGES     (ESP_ERR_NVS_BASE + 0x0d)
#define ESP_ERR_NVS_NEW_VERSION_FOUND (ESP_ERR_NVS_BASE + 0x10)

/* -------------------------------- Types -------------------------------- */

typedef uint32_t nvs_handle_t;

typedef enum {
    NVS_READONLY  = 0,
    NVS_READWRITE = 1,
} nvs_open_mode_t;

/* ------------------------ Public function prototypes ------------------- */

esp_err_t nvs_open(const char *name, nvs_open_mode_t open_mode, nvs_handle_t *out_handle);
void nvs_close(nvs_handle_t handle);
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out_value, size_t *length);
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t length);
esp_err_t nvs_commit(nvs_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* HOST_STUB_NVS_H */

/*** end of file ***/
