/**
 * @file    cfg.c
 * @date    2026-09-07
 * @brief   The device's settings: one validated get/set pair per setting, and a
 *          caller-supplied adapter that puts the record somewhere.
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

/* ------------------------------ Includes ------------------------------- */

#include "cfg.h"

#include "esp_log.h"

#include <string.h>

/* --------------------------- Private macros ---------------------------- */

/* Compiled-in defaults. A device with erased storage runs on exactly these
 * (R-CFG-03), so every field has one — none is "always written anyway". */
#define CFG_DEFAULT_MANIFEST_URL "https://example.invalid/firmware/0xF001.json"
#define CFG_DEFAULT_CHECK_MS     (6U * 60U * 60U * 1000U) /* 6 h */

/** Bytes of the record the CRC covers: everything before the crc32 field. */
#define CFG_CRC_LEN (offsetof(cfg_record_t, crc32))

/* ---------------------------- Private types ---------------------------- */

/* ----------------------------- Static data ----------------------------- */

static const char *TAG = "cfg";

/* --------------------- Private function prototypes --------------------- */

static uint32_t record_crc(const cfg_record_t *rec);
static fw_err_t record_validate(const cfg_record_t *rec, size_t read_len);
static fw_err_t string_get(const char *field, char *out, size_t cap);
static fw_err_t string_set(char *field, size_t field_size, const char *in, bool allow_empty);

/* -------------------------- Public functions --------------------------- */

cfg_record_t cfg_record_default(void) {
    cfg_record_t rec;

    memset(&rec, 0, sizeof(rec));
    rec.version = (uint16_t)CFG_RECORD_VERSION;
    rec.length  = (uint16_t)sizeof(rec);
    (void)strncpy(rec.manifest_url, CFG_DEFAULT_MANIFEST_URL, CFG_URL_MAX - 1U);
    rec.check_interval_ms = CFG_DEFAULT_CHECK_MS;
    rec.crc32             = record_crc(&rec);
    return rec;
}

fw_err_t cfg_init(cfg_t *c, const cfg_store_t *store) {
    if ((c == NULL) || (store == NULL) || (store->load == NULL) || (store->save == NULL)) {
        return FW_ERR_PARAM;
    }
    if (c->is_init) {
        return FW_ERR_STATE;
    }

    cfg_record_t stored;
    size_t read_len = 0U;

    memset(&stored, 0, sizeof(stored));
    const fw_err_t load_err = store->load(store->ctx, &stored, sizeof(stored), &read_len);

    /* "Nothing stored" and "the bytes did not check out" are the two the
     * adapter is allowed to report as normal. Anything else is the caller's
     * problem, logged once here and handed back unchanged (R-LOG-04). */
    if ((load_err != FW_OK) && (load_err != FW_ERR_NOT_FOUND) && (load_err != FW_ERR_CRC)) {
        ESP_LOGE(TAG, "store load failed: %s", fw_err_str(load_err));
        return load_err;
    }

    const fw_err_t verdict = (load_err == FW_OK) ? record_validate(&stored, read_len) : load_err;

    memset(c, 0, sizeof(*c));
    c->store = *store;
    if (verdict == FW_OK) {
        c->rec = stored;
    } else {
        /* Missing, damaged and out of range all mean the same thing: run on
         * the compiled-in defaults and say so, rather than guess which half of
         * the record survived (R-CFG-02, R-CFG-03). */
        ESP_LOGW(TAG, "record unusable (%s), using defaults", fw_err_str(verdict));
        c->rec = cfg_record_default();
    }
    c->is_init = true;
    return FW_OK;
}

fw_err_t cfg_deinit(cfg_t *c) {
    if (c == NULL) {
        return FW_ERR_PARAM;
    }

    /* Nothing is owned here — the adapter's `ctx` is borrowed — so zeroing is
     * the whole release, and it is repeatable (R-LFC-04). */
    memset(c, 0, sizeof(*c));
    return FW_OK;
}

fw_err_t cfg_defaults_set(cfg_t *c) {
    if (c == NULL) {
        return FW_ERR_PARAM;
    }
    if (!c->is_init) {
        return FW_ERR_STATE;
    }

    c->rec = cfg_record_default();
    return FW_OK;
}

fw_err_t cfg_save(cfg_t *c) {
    if (c == NULL) {
        return FW_ERR_PARAM;
    }
    if (!c->is_init) {
        return FW_ERR_STATE;
    }

    /* Stamped here, not in the setters, so the record on the wire to the
     * adapter is always self-describing whatever changed it. */
    c->rec.version = (uint16_t)CFG_RECORD_VERSION;
    c->rec.length  = (uint16_t)sizeof(c->rec);
    c->rec.crc32   = record_crc(&c->rec);

    return c->store.save(c->store.ctx, &c->rec, sizeof(c->rec));
}

fw_err_t cfg_manifest_url_get(const cfg_t *c, char *out, size_t cap) {
    if ((c == NULL) || (out == NULL)) {
        return FW_ERR_PARAM;
    }
    if (!c->is_init) {
        return FW_ERR_STATE;
    }

    return string_get(c->rec.manifest_url, out, cap);
}

fw_err_t cfg_manifest_url_set(cfg_t *c, const char *url) {
    if ((c == NULL) || (url == NULL)) {
        return FW_ERR_PARAM;
    }
    if (!c->is_init) {
        return FW_ERR_STATE;
    }

    /* An empty URL is refused: there is no such manifest, and accepting it
     * would turn a typo into a cycle that checks nothing and reports success.
     * Disabling checks is what a zero interval is for. */
    return string_set(c->rec.manifest_url, CFG_URL_MAX, url, false);
}

fw_err_t cfg_last_ok_fw_version_get(const cfg_t *c, char *out, size_t cap) {
    if ((c == NULL) || (out == NULL)) {
        return FW_ERR_PARAM;
    }
    if (!c->is_init) {
        return FW_ERR_STATE;
    }

    return string_get(c->rec.last_ok_fw_version, out, cap);
}

fw_err_t cfg_last_ok_fw_version_set(cfg_t *c, const char *version) {
    if ((c == NULL) || (version == NULL)) {
        return FW_ERR_PARAM;
    }
    if (!c->is_init) {
        return FW_ERR_STATE;
    }

    /* Empty is legal here and is the shipped default: no image has confirmed
     * itself yet. */
    return string_set(c->rec.last_ok_fw_version, CFG_VERSION_MAX, version, true);
}

fw_err_t cfg_check_interval_ms_get(const cfg_t *c, uint32_t *out_ms) {
    if ((c == NULL) || (out_ms == NULL)) {
        return FW_ERR_PARAM;
    }
    if (!c->is_init) {
        return FW_ERR_STATE;
    }

    *out_ms = c->rec.check_interval_ms;
    return FW_OK;
}

fw_err_t cfg_check_interval_ms_set(cfg_t *c, uint32_t ms) {
    if (c == NULL) {
        return FW_ERR_PARAM;
    }
    if (!c->is_init) {
        return FW_ERR_STATE;
    }

    /* The scheduling horizon, checked where the value arrives rather than at
     * bring-up. See CFG_CHECK_INTERVAL_MAX_MS for why the number lives here. */
    if (ms >= CFG_CHECK_INTERVAL_MAX_MS) {
        return FW_ERR_PARAM;
    }

    c->rec.check_interval_ms = ms;
    return FW_OK;
}

fw_err_t cfg_boot_fail_count_get(const cfg_t *c, uint32_t *out_count) {
    if ((c == NULL) || (out_count == NULL)) {
        return FW_ERR_PARAM;
    }
    if (!c->is_init) {
        return FW_ERR_STATE;
    }

    *out_count = c->rec.boot_fail_count;
    return FW_OK;
}

fw_err_t cfg_boot_fail_count_set(cfg_t *c, uint32_t count) {
    if (c == NULL) {
        return FW_ERR_PARAM;
    }
    if (!c->is_init) {
        return FW_ERR_STATE;
    }

    c->rec.boot_fail_count = count;
    return FW_OK;
}

/* -------------------------- Private functions -------------------------- */

/* Arguments are checked at the public boundary (R-SRC-06); helpers assume it. */

static uint32_t record_crc(const cfg_record_t *rec) {
    /* fw_crc32_le() seeds with ~crc and returns ~crc, so passing 0 gives
     * the standard CRC-32 of the buffer. */
    return fw_crc32_le(0U, (const uint8_t *)rec, (uint32_t)CFG_CRC_LEN);
}

static fw_err_t record_validate(const cfg_record_t *rec, size_t read_len) {
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
    if (rec->version != (uint16_t)CFG_RECORD_VERSION) {
        /* TODO(dtbao): one migration function per version step, chained
         * (R-CFG-04). Until a version 2 exists there is nothing to migrate,
         * and an unknown version falls back to defaults rather than guessing. */
        ESP_LOGW(TAG, "record version unknown: got=%u want=%u", (unsigned)rec->version,
                 (unsigned)CFG_RECORD_VERSION);
        return FW_ERR_NOT_FOUND;
    }

    /* Both strings, not just the URL: a getter reads to the first NUL, so an
     * unterminated field is a read past the end of it. */
    if ((rec->manifest_url[CFG_URL_MAX - 1U] != '\0') ||
        (rec->last_ok_fw_version[CFG_VERSION_MAX - 1U] != '\0')) {
        ESP_LOGW(TAG, "record holds an unterminated string");
        return FW_ERR_CRC;
    }

    /* A record can pass its CRC and still hold a value this build cannot use.
     * Refuse the whole record rather than clamp the field: the defaults are
     * known good, a clamped value is a setting nobody chose. */
    if (rec->check_interval_ms >= CFG_CHECK_INTERVAL_MAX_MS) {
        ESP_LOGW(TAG, "record check_interval_ms past the horizon: %lu",
                 (unsigned long)rec->check_interval_ms);
        return FW_ERR_PARAM;
    }
    return FW_OK;
}

static fw_err_t string_get(const char *field, char *out, size_t cap) {
    const size_t len = strlen(field);

    /* Nothing is copied on a refusal, so the caller's buffer is either the
     * whole answer or exactly what it was. */
    if (cap < (len + 1U)) {
        return FW_ERR_NO_SPACE;
    }

    (void)memcpy(out, field, len + 1U);
    return FW_OK;
}

static fw_err_t string_set(char *field, size_t field_size, const char *in, bool allow_empty) {
    const size_t len = strlen(in);

    if (!allow_empty && (len == 0U)) {
        return FW_ERR_PARAM;
    }
    if (len >= field_size) {
        return FW_ERR_PARAM;
    }

    /* The tail is zeroed, not left as it was: those bytes go into the CRC, so
     * a stale tail would make two records holding the same settings compare
     * unequal and cost a flash write the adapter would otherwise skip. */
    (void)memset(field, 0, field_size);
    /* R-SAN-02, the reason this one is suppressed: the result IS terminated.
     * `len >= field_size` was refused above, so len is at most
     * field_size - 1, and the memset on the line before leaves a zero at every
     * byte this copy does not reach. The check sees the memcpy alone. */
    /* NOLINTNEXTLINE(bugprone-not-null-terminated-result) */
    (void)memcpy(field, in, len);
    return FW_OK;
}

/*** end of file ***/
