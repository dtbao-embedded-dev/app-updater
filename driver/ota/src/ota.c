/**
 * @file    ota.c
 * @date    2026-09-07
 * @brief   ESP-IDF implementation of the OTA slot contract. The only place in
 *          this repo that names `esp_ota_*` or `esp_partition_*`.
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

/* ------------------------------ Includes ------------------------------- */

#include "ota.h"

#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"

#include <string.h>

/* ----------------------------- Static data ----------------------------- */

static const char *TAG = "ota";

/* --------------------- Private function prototypes --------------------- */

static const esp_partition_t *slot_partition(uint8_t slot);
static ota_err_t copy_version(char *out, size_t cap, const char *version);
static ota_err_t from_esp_err(esp_err_t err);

/* -------------------------- Public functions --------------------------- */

const char *ota_err_str(ota_err_t err) {
    switch (err) {
        case OTA_OK:
            return "OTA_OK";
        case OTA_ERR_PARAM:
            return "OTA_ERR_PARAM";
        case OTA_ERR_STATE:
            return "OTA_ERR_STATE";
        case OTA_ERR_NO_SPACE:
            return "OTA_ERR_NO_SPACE";
        case OTA_ERR_NOT_FOUND:
            return "OTA_ERR_NOT_FOUND";
        case OTA_ERR_IO:
            return "OTA_ERR_IO";
        default:
            return "OTA_ERR_UNKNOWN";
    }
}

ota_err_t ota_slot_size_get(uint8_t slot, uint32_t *out_size) {
    if ((out_size == NULL) || (slot >= OTA_SLOT_COUNT)) {
        return OTA_ERR_PARAM;
    }

    const esp_partition_t *part = slot_partition(slot);
    if (part == NULL) {
        return OTA_ERR_NOT_FOUND;
    }

    *out_size = part->size;
    return OTA_OK;
}

ota_err_t ota_slot_version_get(uint8_t slot, char *out, size_t cap) {
    if ((out == NULL) || (cap == 0U) || (slot >= OTA_SLOT_COUNT)) {
        return OTA_ERR_PARAM;
    }

    const esp_partition_t *part = slot_partition(slot);
    if (part == NULL) {
        return OTA_ERR_NOT_FOUND;
    }

    /* A slot that was never written has no readable header, which the SDK
     * reports as a plain failure - it is "no image", not a broken flash, so it
     * is not logged as one. */
    esp_app_desc_t desc;
    if (esp_ota_get_partition_description(part, &desc) != ESP_OK) {
        return OTA_ERR_NOT_FOUND;
    }

    return copy_version(out, cap, desc.version);
}

ota_err_t ota_running_slot_get(uint8_t *out_slot) {
    if (out_slot == NULL) {
        return OTA_ERR_PARAM;
    }

    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running == NULL) {
        return OTA_ERR_STATE;
    }

    for (uint8_t slot = 0U; slot < OTA_SLOT_COUNT; ++slot) {
        const esp_partition_t *part = slot_partition(slot);
        if ((part != NULL) && (part->subtype == running->subtype)) {
            *out_slot = slot;
            return OTA_OK;
        }
    }

    /* The bootloader started something this driver cannot name, which no
     * partitions.csv in this repo produces. */
    ESP_LOGE(TAG, "running partition subtype 0x%02X is no app slot", (unsigned)running->subtype);
    return OTA_ERR_STATE;
}

ota_err_t ota_running_version_get(char *out, size_t cap) {
    if ((out == NULL) || (cap == 0U)) {
        return OTA_ERR_PARAM;
    }

    const esp_app_desc_t *self = esp_app_get_description();
    if (self == NULL) {
        return OTA_ERR_IO;
    }

    return copy_version(out, cap, self->version);
}

ota_err_t ota_boot_slot_set(uint8_t slot) {
    if (slot >= OTA_SLOT_COUNT) {
        return OTA_ERR_PARAM;
    }

    const esp_partition_t *part = slot_partition(slot);
    if (part == NULL) {
        return OTA_ERR_NOT_FOUND;
    }

    const esp_err_t err = esp_ota_set_boot_partition(part);
    if (err != ESP_OK) {
        /* A slot whose image does not validate is a state, not a fault: the
         * caller can fix it by putting an image there. */
        ESP_LOGW(TAG, "arm slot %u failed: esp_err=0x%x", (unsigned)slot, (unsigned)err);
        return OTA_ERR_STATE;
    }
    return OTA_OK;
}

ota_err_t ota_pending_verify_is(bool *out_pending) {
    if (out_pending == NULL) {
        return OTA_ERR_PARAM;
    }
    *out_pending = false;

    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    const esp_err_t err = esp_ota_get_state_partition(running, &state);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "read boot state failed: esp_err=0x%x", (unsigned)err);
        return from_esp_err(err);
    }

    *out_pending = (state == ESP_OTA_IMG_PENDING_VERIFY);
    return OTA_OK;
}

ota_err_t ota_mark_valid(void) {
    const esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err == ESP_ERR_INVALID_STATE) {
        return OTA_ERR_STATE;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "confirm image failed: esp_err=0x%x", (unsigned)err);
        return from_esp_err(err);
    }
    return OTA_OK;
}

ota_err_t ota_session_begin(uint8_t slot, uint32_t img_size, ota_session_t *out) {
    if (out == NULL) {
        return OTA_ERR_PARAM;
    }
    *out = OTA_SESSION_NONE;

    if ((slot >= OTA_SLOT_COUNT) || (img_size == 0U)) {
        return OTA_ERR_PARAM;
    }

    const esp_partition_t *part = slot_partition(slot);
    if (part == NULL) {
        return OTA_ERR_NOT_FOUND;
    }
    if (img_size > part->size) {
        return OTA_ERR_NO_SPACE;
    }

    /* Writing the slot the code is executing from is what the two-slot layout
     * exists to prevent, so it is refused here as well as by every caller. */
    const esp_partition_t *running = esp_ota_get_running_partition();
    if ((running != NULL) && (running->subtype == part->subtype)) {
        return OTA_ERR_STATE;
    }

    esp_ota_handle_t handle = 0;
    const esp_err_t err     = esp_ota_begin(part, (size_t)img_size, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "begin slot=%u size=%lu failed: esp_err=0x%x", (unsigned)slot,
                 (unsigned long)img_size, (unsigned)err);
        return from_esp_err(err);
    }

    *out = (ota_session_t)handle;
    return OTA_OK;
}

ota_err_t ota_session_write(ota_session_t session, const void *data, size_t len) {
    if ((session == OTA_SESSION_NONE) || (data == NULL) || (len == 0U)) {
        return OTA_ERR_PARAM;
    }

    const esp_err_t err = esp_ota_write((esp_ota_handle_t)session, data, len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "write %u bytes failed: esp_err=0x%x", (unsigned)len, (unsigned)err);
        return from_esp_err(err);
    }
    return OTA_OK;
}

ota_err_t ota_session_end(ota_session_t session) {
    if (session == OTA_SESSION_NONE) {
        return OTA_ERR_PARAM;
    }

    /* esp_ota_end() frees the session whether the image validated or not, so
     * the handle is gone either way and must not be aborted a second time -
     * which is exactly what this function's contract promises its caller. */
    const esp_err_t err = esp_ota_end((esp_ota_handle_t)session);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "end failed: esp_err=0x%x", (unsigned)err);
        return from_esp_err(err);
    }
    return OTA_OK;
}

ota_err_t ota_session_abort(ota_session_t session) {
    if (session == OTA_SESSION_NONE) {
        return OTA_ERR_PARAM;
    }

    const esp_err_t err = esp_ota_abort((esp_ota_handle_t)session);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "abort failed: esp_err=0x%x", (unsigned)err);
        return from_esp_err(err);
    }
    return OTA_OK;
}

/* -------------------------- Private functions -------------------------- */

/* Slot index to partition, the one place the mapping lives. Slots are the OTA
 * app subtypes in order, so slot N is `ota_N` - no label is matched, because a
 * product that renames its partitions should not have to edit this driver. */
static const esp_partition_t *slot_partition(uint8_t slot) {
    const esp_partition_subtype_t subtype =
        (esp_partition_subtype_t)((int)ESP_PARTITION_SUBTYPE_APP_OTA_0 + (int)slot);

    return esp_partition_find_first(ESP_PARTITION_TYPE_APP, subtype, NULL);
}

/* Bounded copy that always terminates. Hand-rolled rather than strncpy to keep
 * the truncation explicit instead of arguing with -Wstringop-truncation. */
static ota_err_t copy_version(char *out, size_t cap, const char *version) {
    memset(out, 0, cap);
    for (size_t i = 0; (i < (cap - 1U)) && (version[i] != '\0'); ++i) {
        out[i] = version[i];
    }
    return OTA_OK;
}

/* The vendor status never escapes this module (R-ERR-04). Silent on purpose:
 * every caller either logs the raw value with context this mapper does not
 * have - which slot, how many bytes - or hands the code up to a layer that
 * logs it. Logging here as well would double every failure line. */
static ota_err_t from_esp_err(esp_err_t err) {
    switch (err) {
        case ESP_ERR_INVALID_ARG:
            return OTA_ERR_PARAM;
        case ESP_ERR_INVALID_STATE:
            return OTA_ERR_STATE;
        case ESP_ERR_INVALID_SIZE:
            return OTA_ERR_NO_SPACE;
        case ESP_ERR_NOT_FOUND:
            return OTA_ERR_NOT_FOUND;
        default:
            return OTA_ERR_IO;
    }
}

/*** end of file ***/
