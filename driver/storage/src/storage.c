/**
 * @file    storage.c
 * @date    2026-09-07
 * @brief   Stores one opaque blob in non-volatile storage and hands it back.
 *          Knows nothing about what is in it.
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

/* ------------------------------ Includes ------------------------------- */

#include "storage.h"

#include "esp_err.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <string.h>

/* --------------------------- Private macros ---------------------------- */

/* Where the blob goes. The namespace and key are the module's only compiled-in
 * defaults: what is *in* the blob is somebody else's business (see
 * middleware/cfg). */
#define STORAGE_DEFAULT_NAMESPACE "updater"
#define STORAGE_DEFAULT_KEY       "record"

/* ---------------------------- Private types ---------------------------- */

/* ----------------------------- Static data ----------------------------- */

static const char *TAG = "storage";

/* --------------------- Private function prototypes --------------------- */

static storage_err_t from_esp_err(esp_err_t err);

/* -------------------------- Public functions --------------------------- */

const char *storage_err_str(storage_err_t err) {
    switch (err) {
        case STORAGE_OK:
            return "STORAGE_OK";
        case STORAGE_ERR_PARAM:
            return "STORAGE_ERR_PARAM";
        case STORAGE_ERR_STATE:
            return "STORAGE_ERR_STATE";
        case STORAGE_ERR_NO_SPACE:
            return "STORAGE_ERR_NO_SPACE";
        case STORAGE_ERR_NOT_FOUND:
            return "STORAGE_ERR_NOT_FOUND";
        case STORAGE_ERR_IO:
            return "STORAGE_ERR_IO";
        case STORAGE_ERR_CRC:
            return "STORAGE_ERR_CRC";
        case STORAGE_ERR_NO_MEM:
            return "STORAGE_ERR_NO_MEM";
        default:
            return "STORAGE_ERR_UNKNOWN";
    }
}

storage_cfg_t storage_cfg_default(void) {
    const storage_cfg_t cfg = {
        .nvs_namespace = STORAGE_DEFAULT_NAMESPACE,
        .nvs_key       = STORAGE_DEFAULT_KEY,
    };
    return cfg;
}

storage_err_t storage_init(storage_t *st, const storage_cfg_t *cfg) {
    if ((st == NULL) || (cfg == NULL) || (cfg->nvs_namespace == NULL) || (cfg->nvs_key == NULL)) {
        return STORAGE_ERR_PARAM;
    }
    if (st->is_init) {
        return STORAGE_ERR_STATE;
    }

    /* The driver owns its subsystem: a caller that has to remember to call
     * nvs_flash_init() first is a caller that will forget. A partition left
     * unusable by a truncated or newer-format image is erased once - losing
     * the settings, which the caller recovers from by taking its defaults,
     * where refusing to boot has no recovery at all. */
    esp_err_t flash_err = nvs_flash_init();
    if ((flash_err == ESP_ERR_NVS_NO_FREE_PAGES) || (flash_err == ESP_ERR_NVS_NEW_VERSION_FOUND)) {
        ESP_LOGW(TAG, "nvs unusable (esp_err=0x%x), erasing", (unsigned)flash_err);
        if (nvs_flash_erase() != ESP_OK) {
            return STORAGE_ERR_IO;
        }
        flash_err = nvs_flash_init();
    }
    if (flash_err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init failed: esp_err=0x%x", (unsigned)flash_err);
        return from_esp_err(flash_err);
    }

    nvs_handle_t handle = 0;
    const esp_err_t err = nvs_open(cfg->nvs_namespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open ns=%s failed: esp_err=0x%x", cfg->nvs_namespace, (unsigned)err);
        return from_esp_err(err);
    }

    memset(st, 0, sizeof(*st));
    st->nvs_handle = (uint32_t)handle;
    st->nvs_key    = cfg->nvs_key;
    st->is_init    = true;
    return STORAGE_OK;
}

storage_err_t storage_deinit(storage_t *st) {
    if (st == NULL) {
        return STORAGE_ERR_PARAM;
    }

    /* Repeatable and safe on a half-built instance (R-LFC-04). */
    if (st->is_init) {
        nvs_close((nvs_handle_t)st->nvs_handle);
    }
    memset(st, 0, sizeof(*st));
    return STORAGE_OK;
}

storage_err_t storage_blob_load(storage_t *st, void *out, size_t cap, size_t *out_len) {
    if ((st == NULL) || (out == NULL) || (out_len == NULL) || (cap == 0U)) {
        return STORAGE_ERR_PARAM;
    }
    if (cap > STORAGE_BLOB_MAX) {
        return STORAGE_ERR_NO_SPACE;
    }
    if (!st->is_init) {
        return STORAGE_ERR_STATE;
    }

    size_t len          = cap;
    const esp_err_t err = nvs_get_blob((nvs_handle_t)st->nvs_handle, st->nvs_key, out, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return STORAGE_ERR_NOT_FOUND;
    }
    if (err == ESP_ERR_NVS_INVALID_LENGTH) {
        /* NVS refused because the stored blob is bigger than `cap`, so none of
         * it was copied. A blob of a length this build does not use is not one
         * this build wrote - report it as damaged, which is the verdict that
         * makes the caller fall back to its defaults instead of failing
         * bring-up on a record it could never have parsed anyway. */
        ESP_LOGW(TAG, "stored blob does not fit %u bytes", (unsigned)cap);
        return STORAGE_ERR_CRC;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_get_blob failed: esp_err=0x%x", (unsigned)err);
        return from_esp_err(err);
    }

    *out_len = len;
    return STORAGE_OK;
}

storage_err_t storage_blob_save(storage_t *st, const void *data, size_t len) {
    if ((st == NULL) || (data == NULL) || (len == 0U)) {
        return STORAGE_ERR_PARAM;
    }
    if (len > STORAGE_BLOB_MAX) {
        return STORAGE_ERR_NO_SPACE;
    }
    if (!st->is_init) {
        return STORAGE_ERR_STATE;
    }

    /* Read-compare-write: NVS wear is measured in sectors, not in calls
     * (R-CFG-05). The compare is cheap; the erase is not. */
    uint8_t current[STORAGE_BLOB_MAX];
    size_t current_len = 0U;
    if (storage_blob_load(st, current, len, &current_len) == STORAGE_OK) {
        if ((current_len == len) && (memcmp(current, data, len) == 0)) {
            return STORAGE_OK;
        }
    }

    const esp_err_t set_err = nvs_set_blob((nvs_handle_t)st->nvs_handle, st->nvs_key, data, len);
    if (set_err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_blob failed: esp_err=0x%x", (unsigned)set_err);
        return from_esp_err(set_err);
    }

    /* NVS commits the new copy before dropping the old one, so a power cut
     * here leaves the previous blob readable (R-CFG-06). */
    const esp_err_t commit_err = nvs_commit((nvs_handle_t)st->nvs_handle);
    if (commit_err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_commit failed: esp_err=0x%x", (unsigned)commit_err);
        return from_esp_err(commit_err);
    }
    return STORAGE_OK;
}

/* -------------------------- Private functions -------------------------- */

/* Arguments are checked at the public boundary (R-SRC-06); helpers assume it. */

/* R-ERR-04: the vendor code stops here, logged at the call site above. */
static storage_err_t from_esp_err(esp_err_t err) {
    switch (err) {
        case ESP_OK:
            return STORAGE_OK;
        case ESP_ERR_INVALID_ARG:
            return STORAGE_ERR_PARAM;
        case ESP_ERR_INVALID_STATE:
            return STORAGE_ERR_STATE;
        case ESP_ERR_NVS_NOT_FOUND:
            return STORAGE_ERR_NOT_FOUND;
        case ESP_ERR_NVS_NOT_ENOUGH_SPACE:
            return STORAGE_ERR_NO_SPACE;
        case ESP_ERR_NO_MEM:
            return STORAGE_ERR_NO_MEM;
        default:
            return STORAGE_ERR_IO;
    }
}

/*** end of file ***/
