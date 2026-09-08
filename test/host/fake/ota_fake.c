/**
 * @file    ota_fake.c
 * @date    2026-09-07
 * @brief   Host fake of `driver/ota`: the slot table, the write session, and
 *          the boot-confirmation state, in RAM.
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

/* ------------------------------ Includes ------------------------------- */

#include "ota_fake.h"

#include "esp_rom_crc.h"

#include <string.h>

/* --------------------------- Private macros ---------------------------- */

/* The two app slots of workspace/0xF001/partitions.csv at their real sizes: a
 * test that says "this image does not fit" should be arguing with the same
 * numbers the board has. */
#define SLOT_0_SIZE 0x200000U /* app_updater,  2 MB      */
#define SLOT_1_SIZE 0xDE0000U /* app_firmware, 13.875 MB */

/* ---------------------------- Private types ---------------------------- */

typedef struct {
    bool present;
    bool has_image;
    uint32_t size;
    char version[OTA_VERSION_MAX];
} slot_t;

/* ----------------------------- Static data ----------------------------- */

static slot_t s_slots[OTA_SLOT_COUNT];
static uint8_t s_running;
static char s_running_version[OTA_VERSION_MAX];
static bool s_pending_verify;
static bool s_marked_valid;
static int s_armed;

static ota_err_t s_fail_boot_slot_set;
static ota_err_t s_fail_begin;
static ota_err_t s_fail_write;
static ota_err_t s_fail_end;

/* `s_open` counts sessions opened and not yet closed, so a test can prove
 * UPG_BEGIN freed the previous session instead of leaking one per retry - the
 * exact defect the source spec warns about. */
static uint32_t s_begin_count;
static uint32_t s_open;
static uint32_t s_written;
static uint32_t s_crc;
static ota_session_t s_next_session;
static bool s_finalised;
static bool s_aborted;

/* --------------------- Private function prototypes --------------------- */

static slot_t *slot_of(uint8_t slot);
static void set_version(char *dst, const char *src);

/* ------------------------- The control surface ------------------------- */

void ota_fake_reset(void) {
    memset(s_slots, 0, sizeof(s_slots));

    s_slots[0].present   = true;
    s_slots[0].has_image = true;
    s_slots[0].size      = SLOT_0_SIZE;
    set_version(s_slots[0].version, "0.1.0");

    s_slots[1].present   = true;
    s_slots[1].has_image = true;
    s_slots[1].size      = SLOT_1_SIZE;
    set_version(s_slots[1].version, "2.4.0");

    /* The updater is what runs on this product, so that is the honest default. */
    s_running = 0U;
    set_version(s_running_version, "0.1.0");

    s_pending_verify = false;
    s_marked_valid   = false;
    s_armed          = -1;

    s_fail_boot_slot_set = OTA_OK;
    s_fail_begin         = OTA_OK;
    s_fail_write         = OTA_OK;
    s_fail_end           = OTA_OK;

    s_begin_count  = 0U;
    s_open         = 0U;
    s_written      = 0U;
    s_crc          = 0U;
    s_next_session = 1U; /* OTA_SESSION_NONE is left meaning "no session" */
    s_finalised    = false;
    s_aborted      = false;
}

void ota_fake_set_running_slot(uint8_t slot) {
    s_running = slot;
}

void ota_fake_set_running_version(const char *version) {
    set_version(s_running_version, version);
}

void ota_fake_set_slot_version(uint8_t slot, const char *version) {
    slot_t *entry = slot_of(slot);
    if (entry == NULL) {
        return;
    }
    entry->has_image = (version != NULL);
    set_version(entry->version, version);
}

void ota_fake_set_slot_size(uint8_t slot, uint32_t size) {
    slot_t *entry = slot_of(slot);
    if (entry != NULL) {
        entry->size = size;
    }
}

void ota_fake_remove_slot(uint8_t slot) {
    slot_t *entry = slot_of(slot);
    if (entry != NULL) {
        entry->present = false;
    }
}

void ota_fake_set_pending_verify(bool pending) {
    s_pending_verify = pending;
}

void ota_fake_fail_boot_slot_set(ota_err_t err) {
    s_fail_boot_slot_set = err;
}

void ota_fake_fail_begin(ota_err_t err) {
    s_fail_begin = err;
}

void ota_fake_fail_write(ota_err_t err) {
    s_fail_write = err;
}

void ota_fake_fail_end(ota_err_t err) {
    s_fail_end = err;
}

int ota_fake_boot_slot_armed(void) {
    return s_armed;
}

bool ota_fake_marked_valid(void) {
    return s_marked_valid;
}

uint32_t ota_fake_begin_count(void) {
    return s_begin_count;
}

uint32_t ota_fake_open_sessions(void) {
    return s_open;
}

uint32_t ota_fake_written(void) {
    return s_written;
}

bool ota_fake_finalised(void) {
    return s_finalised;
}

bool ota_fake_aborted(void) {
    return s_aborted;
}

uint32_t ota_fake_crc(void) {
    return s_crc;
}

/* --------------------------- The fake proper --------------------------- */

const char *ota_err_str(ota_err_t err) {
    switch (err) {
        case OTA_OK:            return "OTA_OK";
        case OTA_ERR_PARAM:     return "OTA_ERR_PARAM";
        case OTA_ERR_STATE:     return "OTA_ERR_STATE";
        case OTA_ERR_NO_SPACE:  return "OTA_ERR_NO_SPACE";
        case OTA_ERR_NOT_FOUND: return "OTA_ERR_NOT_FOUND";
        case OTA_ERR_IO:        return "OTA_ERR_IO";
        default:                return "OTA_ERR_UNKNOWN";
    }
}

ota_err_t ota_slot_size_get(uint8_t slot, uint32_t *out_size) {
    if (out_size == NULL) {
        return OTA_ERR_PARAM;
    }

    const slot_t *entry = slot_of(slot);
    if (entry == NULL) {
        return OTA_ERR_PARAM;
    }
    if (!entry->present) {
        return OTA_ERR_NOT_FOUND;
    }

    *out_size = entry->size;
    return OTA_OK;
}

ota_err_t ota_slot_version_get(uint8_t slot, char *out, size_t cap) {
    if ((out == NULL) || (cap == 0U)) {
        return OTA_ERR_PARAM;
    }

    const slot_t *entry = slot_of(slot);
    if (entry == NULL) {
        return OTA_ERR_PARAM;
    }
    if (!entry->present) {
        return OTA_ERR_NOT_FOUND;
    }

    /* The running slot answers from the image header, like the target does. */
    const char *version = (slot == s_running) ? s_running_version : entry->version;
    if ((slot != s_running) && !entry->has_image) {
        return OTA_ERR_NOT_FOUND;
    }

    memset(out, 0, cap);
    (void)strncpy(out, version, cap - 1U);
    return OTA_OK;
}

ota_err_t ota_running_slot_get(uint8_t *out_slot) {
    if (out_slot == NULL) {
        return OTA_ERR_PARAM;
    }
    if (s_running >= OTA_SLOT_COUNT) {
        return OTA_ERR_STATE;
    }

    *out_slot = s_running;
    return OTA_OK;
}

ota_err_t ota_running_version_get(char *out, size_t cap) {
    if ((out == NULL) || (cap == 0U)) {
        return OTA_ERR_PARAM;
    }

    memset(out, 0, cap);
    (void)strncpy(out, s_running_version, cap - 1U);
    return OTA_OK;
}

ota_err_t ota_boot_slot_set(uint8_t slot) {
    const slot_t *entry = slot_of(slot);
    if (entry == NULL) {
        return OTA_ERR_PARAM;
    }
    if (!entry->present) {
        return OTA_ERR_NOT_FOUND;
    }
    if (s_fail_boot_slot_set != OTA_OK) {
        return s_fail_boot_slot_set;
    }

    s_armed = (int)slot;
    return OTA_OK;
}

ota_err_t ota_pending_verify_is(bool *out_pending) {
    if (out_pending == NULL) {
        return OTA_ERR_PARAM;
    }
    *out_pending = s_pending_verify;
    return OTA_OK;
}

ota_err_t ota_mark_valid(void) {
    if (!s_pending_verify) {
        return OTA_ERR_STATE;
    }
    s_pending_verify = false;
    s_marked_valid   = true;
    return OTA_OK;
}

ota_err_t ota_session_begin(uint8_t slot, uint32_t img_size, ota_session_t *out) {
    if (out == NULL) {
        return OTA_ERR_PARAM;
    }
    *out = OTA_SESSION_NONE;

    const slot_t *entry = slot_of(slot);
    if ((entry == NULL) || (img_size == 0U)) {
        return OTA_ERR_PARAM;
    }
    if (!entry->present) {
        return OTA_ERR_NOT_FOUND;
    }
    if (slot == s_running) {
        return OTA_ERR_STATE;
    }
    if (s_fail_begin != OTA_OK) {
        return s_fail_begin;
    }
    if (img_size > entry->size) {
        /* The real driver refuses this too, but the command layer is supposed
         * to have caught it first - the point of the fake is that a test can
         * tell which of the two did. */
        return OTA_ERR_NO_SPACE;
    }

    s_begin_count += 1U;
    s_open += 1U;
    s_written = 0U;
    s_crc     = 0U;
    *out      = s_next_session;
    s_next_session += 1U;
    return OTA_OK;
}

ota_err_t ota_session_write(ota_session_t session, const void *data, size_t len) {
    if ((session == OTA_SESSION_NONE) || (data == NULL) || (len == 0U)) {
        return OTA_ERR_PARAM;
    }
    if (s_open == 0U) {
        return OTA_ERR_STATE;
    }
    if (s_fail_write != OTA_OK) {
        return s_fail_write;
    }

    /* Folded rather than stored: the tests care that the right bytes arrived
     * in the right order, not about keeping 13 MB of them around. */
    s_crc = esp_rom_crc32_le(s_crc, (const uint8_t *)data, (uint32_t)len);
    s_written += (uint32_t)len;
    return OTA_OK;
}

ota_err_t ota_session_end(ota_session_t session) {
    if (session == OTA_SESSION_NONE) {
        return OTA_ERR_PARAM;
    }
    if (s_open == 0U) {
        return OTA_ERR_STATE;
    }

    /* Released either way, success or failure, exactly as the contract says. */
    s_open -= 1U;
    if (s_fail_end != OTA_OK) {
        return s_fail_end;
    }

    s_finalised = true;
    return OTA_OK;
}

ota_err_t ota_session_abort(ota_session_t session) {
    if (session == OTA_SESSION_NONE) {
        return OTA_ERR_PARAM;
    }
    if (s_open == 0U) {
        return OTA_ERR_STATE;
    }

    s_open -= 1U;
    s_aborted = true;
    return OTA_OK;
}

/* -------------------------- Private functions -------------------------- */

static slot_t *slot_of(uint8_t slot) {
    return (slot < OTA_SLOT_COUNT) ? &s_slots[slot] : NULL;
}

static void set_version(char *dst, const char *src) {
    memset(dst, 0, OTA_VERSION_MAX);
    if (src != NULL) {
        (void)strncpy(dst, src, OTA_VERSION_MAX - 1U);
    }
}

/*** end of file ***/
