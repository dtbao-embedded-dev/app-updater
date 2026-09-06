/**
 * @file    storage.c
 * @author  dtbao
 * @date    2026-09-06
 * @brief   Persists the updater's settings and boot record in NVS, versioned
 *          and CRC-protected.
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

/* ------------------------------ Includes ------------------------------- */

#include "storage.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_rom_crc.h"
#include "nvs.h"

#include <string.h>

/* --------------------------- Private macros ---------------------------- */

/* Compiled-in defaults. A device with erased flash boots on exactly these
 * (R-CFG-03), so every field has one — none is "always written anyway". */
#define STORAGE_DEFAULT_NAMESPACE    "updater"
#define STORAGE_DEFAULT_KEY          "record"
#define STORAGE_DEFAULT_MANIFEST_URL "https://example.invalid/firmware/0xF001.json"
#define STORAGE_DEFAULT_CHECK_MS     (6U * 60U * 60U * 1000U) /* 6 h */

/** Bytes of the record the CRC covers: everything before the crc32 field. */
#define STORAGE_CRC_LEN (offsetof(storage_record_t, crc32))

/* ---------------------------- Private types ---------------------------- */

/* ----------------------------- Static data ----------------------------- */

static const char *TAG = "storage";

/* --------------------- Private function prototypes --------------------- */

static uint32_t record_crc(const storage_record_t *rec);
static fw_err_t record_validate(const storage_record_t *rec, size_t read_len);
static fw_err_t from_esp_err(esp_err_t err);

/* -------------------------- Public functions --------------------------- */

storage_cfg_t storage_cfg_default(void) {
    const storage_cfg_t cfg = {
        .nvs_namespace = STORAGE_DEFAULT_NAMESPACE,
        .nvs_key       = STORAGE_DEFAULT_KEY,
    };
    return cfg;
}

storage_record_t storage_record_default(void) {
    storage_record_t rec;

    memset(&rec, 0, sizeof(rec));
    rec.version = (uint16_t)STORAGE_RECORD_VERSION;
    rec.length  = (uint16_t)sizeof(rec);
    (void)strncpy(rec.manifest_url, STORAGE_DEFAULT_MANIFEST_URL, STORAGE_URL_MAX - 1U);
    rec.check_interval_ms = STORAGE_DEFAULT_CHECK_MS;
    rec.crc32             = record_crc(&rec);
    return rec;
}

fw_err_t storage_init(storage_t *st, const storage_cfg_t *cfg) {
    if ((st == NULL) || (cfg == NULL) || (cfg->nvs_namespace == NULL) || (cfg->nvs_key == NULL)) {
        return FW_ERR_PARAM;
    }
    if (st->is_init) {
        return FW_ERR_STATE;
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
    return FW_OK;
}

fw_err_t storage_deinit(storage_t *st) {
    if (st == NULL) {
        return FW_ERR_PARAM;
    }

    /* Repeatable and safe on a half-built instance (R-LFC-04). */
    if (st->is_init) {
        nvs_close((nvs_handle_t)st->nvs_handle);
    }
    memset(st, 0, sizeof(*st));
    return FW_OK;
}

fw_err_t storage_record_load(storage_t *st, storage_record_t *out_rec) {
    if ((st == NULL) || (out_rec == NULL)) {
        return FW_ERR_PARAM;
    }
    if (!st->is_init) {
        return FW_ERR_STATE;
    }

    storage_record_t stored;
    size_t len = sizeof(stored);

    memset(&stored, 0, sizeof(stored));
    const esp_err_t err = nvs_get_blob((nvs_handle_t)st->nvs_handle, st->nvs_key, &stored, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return FW_ERR_NOT_FOUND;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_get_blob failed: esp_err=0x%x", (unsigned)err);
        return from_esp_err(err);
    }

    const fw_err_t verdict = record_validate(&stored, len);
    if (verdict != FW_OK) {
        return verdict;
    }

    *out_rec = stored;
    return FW_OK;
}

fw_err_t storage_record_save(storage_t *st, const storage_record_t *rec) {
    if ((st == NULL) || (rec == NULL)) {
        return FW_ERR_PARAM;
    }
    if (!st->is_init) {
        return FW_ERR_STATE;
    }

    storage_record_t to_write = *rec;
    to_write.version          = (uint16_t)STORAGE_RECORD_VERSION;
    to_write.length           = (uint16_t)sizeof(to_write);
    to_write.crc32            = record_crc(&to_write);

    /* Read-compare-write: NVS wear is measured in sectors, not in calls
     * (R-CFG-05). The compare is cheap; the erase is not. */
    storage_record_t current;
    if (storage_record_load(st, &current) == FW_OK) {
        if (memcmp(&current, &to_write, sizeof(current)) == 0) {
            return FW_OK;
        }
    }

    const esp_err_t set_err =
        nvs_set_blob((nvs_handle_t)st->nvs_handle, st->nvs_key, &to_write, sizeof(to_write));
    if (set_err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_blob failed: esp_err=0x%x", (unsigned)set_err);
        return from_esp_err(set_err);
    }

    /* NVS commits the new copy before dropping the old one, so a power cut
     * here leaves the previous record readable (R-CFG-06). */
    const esp_err_t commit_err = nvs_commit((nvs_handle_t)st->nvs_handle);
    if (commit_err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_commit failed: esp_err=0x%x", (unsigned)commit_err);
        return from_esp_err(commit_err);
    }
    return FW_OK;
}

/* -------------------------- Private functions -------------------------- */

/* Arguments are checked at the public boundary (R-SRC-06); helpers assume it. */

static uint32_t record_crc(const storage_record_t *rec) {
    /* esp_rom_crc32_le() seeds with ~crc and returns ~crc, so passing 0 gives
     * the standard CRC-32 of the buffer. */
    return esp_rom_crc32_le(0U, (const uint8_t *)rec, (uint32_t)STORAGE_CRC_LEN);
}

static fw_err_t record_validate(const storage_record_t *rec, size_t read_len) {
    if (read_len != sizeof(*rec)) {
        ESP_LOGW(TAG, "record length mismatch: got=%u want=%u", (unsigned)read_len,
                 (unsigned)sizeof(*rec));
        return FW_ERR_CRC;
    }
    if (rec->crc32 != record_crc(rec)) {
        ESP_LOGW(TAG, "record crc mismatch: got=0x%08lX want=0x%08lX", (unsigned long)rec->crc32,
                 (unsigned long)record_crc(rec));
        return FW_ERR_CRC;
    }
    if (rec->version != (uint16_t)STORAGE_RECORD_VERSION) {
        /* TODO(dtbao): one migration function per version step, chained
         * (R-CFG-04). Until a version 2 exists there is nothing to migrate,
         * and an unknown version falls back to defaults rather than guessing. */
        ESP_LOGW(TAG, "record version unknown: got=%u want=%u", (unsigned)rec->version,
                 (unsigned)STORAGE_RECORD_VERSION);
        return FW_ERR_NOT_FOUND;
    }
    if (rec->manifest_url[STORAGE_URL_MAX - 1U] != '\0') {
        ESP_LOGW(TAG, "record manifest_url is not terminated");
        return FW_ERR_CRC;
    }
    return FW_OK;
}

/* R-ERR-04: the vendor code stops here, logged at the call site above. */
static fw_err_t from_esp_err(esp_err_t err) {
    switch (err) {
        case ESP_OK:
            return FW_OK;
        case ESP_ERR_INVALID_ARG:
            return FW_ERR_PARAM;
        case ESP_ERR_INVALID_STATE:
            return FW_ERR_STATE;
        case ESP_ERR_NVS_NOT_FOUND:
            return FW_ERR_NOT_FOUND;
        case ESP_ERR_NVS_NOT_ENOUGH_SPACE:
            return FW_ERR_NO_SPACE;
        case ESP_ERR_NO_MEM:
            return FW_ERR_NO_MEM;
        default:
            return FW_ERR_IO;
    }
}

/*** end of file ***/
