/**
 * @file    coredump.c
 * @date    2026-09-07
 * @brief   ESP-IDF implementation of the core dump contract. The only place in
 *          this repo that names `esp_core_dump_*`.
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

/* ------------------------------ Includes ------------------------------- */

#include "coredump.h"

#include "esp_core_dump.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_partition.h"

#include <stddef.h>

/* Refuse with an instruction rather than fail confusingly: every
 * `esp_core_dump_*` prototype this file calls lives behind that same guard in
 * `esp_core_dump.h`, so with the option off the errors would all be
 * "undeclared function" and name neither the cause nor the fix. There is no
 * stub alternative worth having either - a driver that always answered "no
 * dump" would be a unit that silently never captures a panic. */
#if !CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH
#error                                                                                             \
    "driver/coredump needs CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y - see workspace/<pid>/sdkconfig.defaults"
#endif

/* ----------------------------- Static data ----------------------------- */

static const char *TAG = "coredump";

/* --------------------- Private function prototypes --------------------- */

static const esp_partition_t *dump_partition(void);
static coredump_err_t from_esp_err(esp_err_t err);

/* -------------------------- Public functions --------------------------- */

const char *coredump_err_str(coredump_err_t err) {
    switch (err) {
        case COREDUMP_OK:
            return "COREDUMP_OK";
        case COREDUMP_ERR_PARAM:
            return "COREDUMP_ERR_PARAM";
        case COREDUMP_ERR_NOT_FOUND:
            return "COREDUMP_ERR_NOT_FOUND";
        case COREDUMP_ERR_IO:
            return "COREDUMP_ERR_IO";
        default:
            return "COREDUMP_ERR_UNKNOWN";
    }
}

coredump_err_t coredump_info_get(coredump_state_t *out_state, uint32_t *out_size) {
    if ((out_state == NULL) || (out_size == NULL)) {
        return COREDUMP_ERR_PARAM;
    }

    *out_state = COREDUMP_ABSENT;
    *out_size  = 0U;

    /* Reads only the 4-byte length field at the head of the partition, so the
     * cheap question - is anything stored at all - is answered cheaply. */
    size_t addr         = 0U;
    size_t size         = 0U;
    const esp_err_t got = esp_core_dump_image_get(&addr, &size);

    if (got == ESP_ERR_NOT_FOUND) {
        /* Blank partition: nothing has ever panicked here. An answer, not a
         * failure, so the caller needs no second call to tell them apart. */
        return COREDUMP_OK;
    }
    if (got == ESP_ERR_INVALID_SIZE) {
        /* The length field itself is neither blank nor plausible, so there is
         * no length to report - but something did write here, and saying so is
         * the whole point of a state distinct from ABSENT. */
        *out_state = COREDUMP_CORRUPT;
        return COREDUMP_OK;
    }
    if (got != ESP_OK) {
        ESP_LOGE(TAG, "image size: %s", esp_err_to_name(got));
        return from_esp_err(got);
    }

    *out_size = (uint32_t)size;

    /* Only now the expensive half: this re-reads every stored byte to compute
     * the checksum. A mismatch is reported, never hidden - the bytes stay
     * readable, because a dump that fails its checksum is exactly the one
     * somebody needs to look at. */
    const esp_err_t checked = esp_core_dump_image_check();
    *out_state              = (checked == ESP_OK) ? COREDUMP_VALID : COREDUMP_CORRUPT;
    if (checked != ESP_OK) {
        ESP_LOGW(TAG, "stored dump of %u bytes fails its checksum: %s", (unsigned)size,
                 esp_err_to_name(checked));
    }
    return COREDUMP_OK;
}

coredump_err_t coredump_read(uint32_t offset, void *out, uint32_t len) {
    if ((out == NULL) || (len == 0U)) {
        return COREDUMP_ERR_PARAM;
    }

    const esp_partition_t *part = dump_partition();
    if (part == NULL) {
        return COREDUMP_ERR_NOT_FOUND;
    }

    /* The bound is the STORED length, not the partition size: past the dump
     * there is only 0xFF padding, which looks like data and is not. The number
     * comes free with the probe that decides whether anything is stored at
     * all, so bounding here costs a caller nothing and saves it from having to
     * ask - which for a chunked read would otherwise mean one full-dump
     * checksum per chunk. */
    size_t addr         = 0U;
    size_t size         = 0U;
    const esp_err_t got = esp_core_dump_image_get(&addr, &size);
    if (got == ESP_ERR_NOT_FOUND) {
        return COREDUMP_ERR_NOT_FOUND;
    }
    if (got != ESP_OK) {
        /* A length field that is neither blank nor plausible: something wrote
         * here but nothing says how much, so no read can be bounded. This is
         * the one flavour of corruption whose bytes stay unreachable. */
        ESP_LOGE(TAG, "image size: %s", esp_err_to_name(got));
        return from_esp_err(got);
    }

    /* Written as a subtraction so a caller passing a huge `offset` cannot make
     * the sum wrap past the check (R-SRC-11). */
    const uint32_t stored = (uint32_t)size;
    if ((len > stored) || (offset > (stored - len))) {
        return COREDUMP_ERR_PARAM;
    }

    /* The SDK writes the dump from partition offset 0, so a dump offset and a
     * partition offset are the same number and nothing is translated. */
    const esp_err_t err = esp_partition_read(part, (size_t)offset, out, (size_t)len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "read %u bytes at %u: %s", (unsigned)len, (unsigned)offset,
                 esp_err_to_name(err));
        return from_esp_err(err);
    }
    return COREDUMP_OK;
}

coredump_err_t coredump_erase(void) {
    const esp_err_t err = esp_core_dump_image_erase();

    /* No partition at all is the one failure worth distinguishing: it means the
     * table this build ran against has no coredump row, which no retry fixes. */
    if (err == ESP_ERR_NOT_FOUND) {
        ESP_LOGE(TAG, "no coredump partition in this table");
        return COREDUMP_ERR_NOT_FOUND;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "erase: %s", esp_err_to_name(err));
        return COREDUMP_ERR_IO;
    }

    /* Erasing a blank partition succeeds, which is what makes a host's
     * read-then-erase safe to retry after a reply is lost. */
    return COREDUMP_OK;
}

coredump_err_t coredump_reason_get(char *out, size_t cap) {
    if ((out == NULL) || (cap == 0U)) {
        return COREDUMP_ERR_PARAM;
    }

    out[0] = '\0';

    const esp_err_t err = esp_core_dump_get_panic_reason(out, cap);
    if (err == ESP_ERR_NOT_FOUND) {
        /* Either nothing is stored, or the stored dump carries no reason note.
         * One code for both: a caller has the same nothing to say either way. */
        return COREDUMP_ERR_NOT_FOUND;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "panic reason: %s", esp_err_to_name(err));
        return from_esp_err(err);
    }
    return COREDUMP_OK;
}

/* -------------------------- Private functions -------------------------- */

/* Arguments are checked at the public boundary (R-SRC-06); helpers assume it. */

/* The subtype is the contract, not the label: `espcoredump` finds its own
 * partition the same way, so a table that renames the row still works and a
 * table with two coredump rows would have the same first-match behaviour here
 * as in the SDK. */
static const esp_partition_t *dump_partition(void) {
    return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP,
                                    NULL);
}

static coredump_err_t from_esp_err(esp_err_t err) {
    switch (err) {
        case ESP_ERR_INVALID_ARG:
        case ESP_ERR_INVALID_SIZE:
            return COREDUMP_ERR_PARAM;
        case ESP_ERR_NOT_FOUND:
            return COREDUMP_ERR_NOT_FOUND;
        default:
            return COREDUMP_ERR_IO;
    }
}

/*** end of file ***/
