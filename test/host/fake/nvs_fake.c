/**
 * @file    nvs_fake.c
 * @date    2026-09-07
 * @brief   Host fake of ESP-IDF NVS: one namespace, one key, one blob, in RAM.
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

/* ------------------------------ Includes ------------------------------- */

#include "nvs_fake.h"

#include "nvs.h"
#include "nvs_flash.h"

#include <string.h>

/* --------------------------- Private macros ---------------------------- */

/** The one handle this fake hands out; 0 stays "no handle". */
#define FAKE_HANDLE 1U

/* ----------------------------- Static data ----------------------------- */

static uint8_t s_blob[NVS_FAKE_CAP];
static size_t s_blob_len;
static bool s_has_blob;
static bool s_is_open;

static uint32_t s_write_count;
static uint32_t s_commit_count;
static uint32_t s_erase_count;
static uint32_t s_flash_init_count;

static esp_err_t s_fail_flash_init;
static esp_err_t s_fail_open;
static esp_err_t s_fail_get;
static esp_err_t s_fail_set;
static esp_err_t s_fail_commit;

/* ------------------------- The control surface ------------------------- */

void nvs_fake_reset(void) {
    memset(s_blob, 0, sizeof(s_blob));
    s_blob_len = 0U;
    s_has_blob = false;
    s_is_open  = false;

    s_write_count      = 0U;
    s_commit_count     = 0U;
    s_erase_count      = 0U;
    s_flash_init_count = 0U;

    s_fail_flash_init = ESP_OK;
    s_fail_open       = ESP_OK;
    s_fail_get        = ESP_OK;
    s_fail_set        = ESP_OK;
    s_fail_commit     = ESP_OK;
}

void nvs_fake_preload(const void *data, size_t len) {
    if ((data == NULL) || (len == 0U) || (len > sizeof(s_blob))) {
        return;
    }
    memcpy(s_blob, data, len);
    s_blob_len = len;
    s_has_blob = true;
}

size_t nvs_fake_stored_len(void) {
    return s_has_blob ? s_blob_len : 0U;
}

const uint8_t *nvs_fake_stored(void) {
    return s_blob;
}

uint32_t nvs_fake_write_count(void) {
    return s_write_count;
}

uint32_t nvs_fake_commit_count(void) {
    return s_commit_count;
}

uint32_t nvs_fake_erase_count(void) {
    return s_erase_count;
}

bool nvs_fake_is_open(void) {
    return s_is_open;
}

void nvs_fake_fail_flash_init(esp_err_t err) {
    s_fail_flash_init = err;
}

void nvs_fake_fail_open(esp_err_t err) {
    s_fail_open = err;
}

void nvs_fake_fail_get(esp_err_t err) {
    s_fail_get = err;
}

void nvs_fake_fail_set(esp_err_t err) {
    s_fail_set = err;
}

void nvs_fake_fail_commit(esp_err_t err) {
    s_fail_commit = err;
}

/* --------------------------- The fake proper --------------------------- */

esp_err_t nvs_flash_init(void) {
    s_flash_init_count += 1U;

    /* A forced failure fires once and then clears, which is what lets a test
     * state "unusable, then usable after the erase" - the exact sequence
     * storage_init() has to survive. */
    if (s_fail_flash_init != ESP_OK) {
        const esp_err_t err = s_fail_flash_init;
        s_fail_flash_init   = ESP_OK;
        return err;
    }
    return ESP_OK;
}

esp_err_t nvs_flash_erase(void) {
    s_erase_count += 1U;
    memset(s_blob, 0, sizeof(s_blob));
    s_blob_len = 0U;
    s_has_blob = false;
    return ESP_OK;
}

esp_err_t nvs_open(const char *name, nvs_open_mode_t open_mode, nvs_handle_t *out_handle) {
    (void)open_mode;

    if ((name == NULL) || (out_handle == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_fail_open != ESP_OK) {
        return s_fail_open;
    }

    *out_handle = FAKE_HANDLE;
    s_is_open   = true;
    return ESP_OK;
}

void nvs_close(nvs_handle_t handle) {
    if (handle == FAKE_HANDLE) {
        s_is_open = false;
    }
}

esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out_value, size_t *length) {
    if ((handle != FAKE_HANDLE) || (key == NULL) || (length == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_fail_get != ESP_OK) {
        return s_fail_get;
    }
    if (!s_has_blob) {
        return ESP_ERR_NVS_NOT_FOUND;
    }

    /* The real call refuses rather than truncating, and copies nothing when it
     * does - which is why storage_blob_load() can report a stored blob of the
     * wrong length as damaged without having read any of it. */
    if (s_blob_len > *length) {
        *length = s_blob_len;
        return ESP_ERR_NVS_INVALID_LENGTH;
    }
    if (out_value == NULL) {
        *length = s_blob_len;
        return ESP_OK;
    }

    memcpy(out_value, s_blob, s_blob_len);
    *length = s_blob_len;
    return ESP_OK;
}

esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t length) {
    if ((handle != FAKE_HANDLE) || (key == NULL) || (value == NULL) || (length == 0U)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (length > sizeof(s_blob)) {
        return ESP_ERR_NVS_NOT_ENOUGH_SPACE;
    }
    if (s_fail_set != ESP_OK) {
        return s_fail_set;
    }

    memcpy(s_blob, value, length);
    s_blob_len = length;
    s_has_blob = true;
    s_write_count += 1U;
    return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t handle) {
    if (handle != FAKE_HANDLE) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_fail_commit != ESP_OK) {
        return s_fail_commit;
    }

    s_commit_count += 1U;
    return ESP_OK;
}

/*** end of file ***/
