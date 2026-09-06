/**
 * @file    esp_fake.c
 * @date    2026-09-06
 * @brief   State behind the ESP-IDF host fakes (R-TST-07).
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

/* ------------------------------ Includes ------------------------------- */

#include "esp_fake.h"

#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_mac.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_rom_crc.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdbool.h>
#include <string.h>

/* --------------------------- Private macros ---------------------------- */

/* The two app slots of workspace/0xF001/partitions.csv, at their real offsets
 * and sizes: a test that says "this image does not fit" should be arguing with
 * the same numbers the board has. */
#define OTA_0_ADDRESS 0x20000U
#define OTA_0_SIZE    0x200000U /* app_updater,  2 MB      */
#define OTA_1_ADDRESS 0x220000U
#define OTA_1_SIZE    0xDE0000U /* app_firmware, 13.875 MB */

#define SLOT_COUNT 2

/* ---------------------------- Private types ---------------------------- */

typedef struct {
    esp_partition_t part;
    bool present;
    bool has_image;
    char version[32];
} slot_t;

/* ----------------------------- Static data ----------------------------- */

static slot_t s_slots[SLOT_COUNT];
static esp_partition_subtype_t s_running = ESP_PARTITION_SUBTYPE_APP_OTA_0;
static esp_app_desc_t s_self;
static uint8_t s_mac[3][6];
static esp_err_t s_fail_read_mac;
static esp_err_t s_fail_set_boot;
static uint32_t s_restart_count;
static esp_partition_subtype_t s_armed;
static uint32_t s_delay_total_ms;

/* The OTA write session. `s_ota_open` counts sessions opened and not yet
 * closed, so a test can prove UPG_BEGIN freed the previous handle instead of
 * leaking one per retry - the exact defect the source spec warns about. */
static uint32_t s_ota_begin_count;
static uint32_t s_ota_open;
static uint32_t s_ota_written;
static uint32_t s_ota_crc;
static uint32_t s_ota_next_handle;
static bool s_ota_finalised;
static bool s_ota_aborted;
static esp_err_t s_fail_ota_begin;
static esp_err_t s_fail_ota_write;
static esp_err_t s_fail_ota_end;

/* --------------------- Private function prototypes --------------------- */

static slot_t *slot_of(esp_partition_subtype_t subtype);

/* -------------------------- Public functions --------------------------- */

void esp_fake_reset(void) {
    memset(s_slots, 0, sizeof(s_slots));

    s_slots[0].part.type    = ESP_PARTITION_TYPE_APP;
    s_slots[0].part.subtype = ESP_PARTITION_SUBTYPE_APP_OTA_0;
    s_slots[0].part.address = OTA_0_ADDRESS;
    s_slots[0].part.size    = OTA_0_SIZE;
    (void)strcpy(s_slots[0].part.label, "app_updater");
    s_slots[0].present   = true;
    s_slots[0].has_image = true;
    (void)strcpy(s_slots[0].version, "0.1.0");

    s_slots[1].part.type    = ESP_PARTITION_TYPE_APP;
    s_slots[1].part.subtype = ESP_PARTITION_SUBTYPE_APP_OTA_1;
    s_slots[1].part.address = OTA_1_ADDRESS;
    s_slots[1].part.size    = OTA_1_SIZE;
    (void)strcpy(s_slots[1].part.label, "app_firmware");
    s_slots[1].present   = true;
    s_slots[1].has_image = true;
    (void)strcpy(s_slots[1].version, "2.4.0");

    /* The updater is what runs on this product, so that is the honest default. */
    s_running = ESP_PARTITION_SUBTYPE_APP_OTA_0;

    memset(&s_self, 0, sizeof(s_self));
    (void)strcpy(s_self.version, "0.1.0");
    (void)strcpy(s_self.project_name, "app_updater");

    memset(s_mac, 0, sizeof(s_mac));
    s_mac[ESP_MAC_WIFI_STA][0] = 0xAAU;
    s_mac[ESP_MAC_WIFI_STA][5] = 0x01U;
    s_mac[ESP_MAC_BT][0]       = 0xAAU;
    s_mac[ESP_MAC_BT][5]       = 0x02U;

    s_fail_read_mac  = ESP_OK;
    s_fail_set_boot  = ESP_OK;
    s_restart_count  = 0U;
    s_armed          = (esp_partition_subtype_t)0;
    s_delay_total_ms = 0U;

    s_ota_begin_count = 0U;
    s_ota_open        = 0U;
    s_ota_written     = 0U;
    s_ota_crc         = 0U;
    s_ota_next_handle = 1U; /* 0 is left meaning "no handle" */
    s_ota_finalised   = false;
    s_ota_aborted     = false;
    s_fail_ota_begin  = ESP_OK;
    s_fail_ota_write  = ESP_OK;
    s_fail_ota_end    = ESP_OK;
}

void esp_fake_set_running_slot(esp_partition_subtype_t subtype) {
    s_running = subtype;
}

void esp_fake_set_self_version(const char *version) {
    memset(s_self.version, 0, sizeof(s_self.version));
    if (version != NULL) {
        (void)strncpy(s_self.version, version, sizeof(s_self.version) - 1U);
    }
}

void esp_fake_set_slot_version(esp_partition_subtype_t subtype, const char *version) {
    slot_t *slot = slot_of(subtype);
    if (slot == NULL) {
        return;
    }
    memset(slot->version, 0, sizeof(slot->version));
    slot->has_image = (version != NULL);
    if (version != NULL) {
        (void)strncpy(slot->version, version, sizeof(slot->version) - 1U);
    }
}

void esp_fake_set_slot_size(esp_partition_subtype_t subtype, uint32_t size) {
    slot_t *slot = slot_of(subtype);
    if (slot != NULL) {
        slot->part.size = size;
    }
}

void esp_fake_remove_slot(esp_partition_subtype_t subtype) {
    slot_t *slot = slot_of(subtype);
    if (slot != NULL) {
        slot->present = false;
    }
}

void esp_fake_set_mac(esp_mac_type_t type, const uint8_t *mac) {
    if ((mac != NULL) && ((size_t)type < (sizeof(s_mac) / sizeof(s_mac[0])))) {
        memcpy(s_mac[type], mac, 6U);
    }
}

void esp_fake_fail_read_mac(esp_err_t err) {
    s_fail_read_mac = err;
}

void esp_fake_fail_set_boot(esp_err_t err) {
    s_fail_set_boot = err;
}

uint32_t esp_fake_restart_count(void) {
    return s_restart_count;
}

esp_partition_subtype_t esp_fake_boot_slot_armed(void) {
    return s_armed;
}

uint32_t esp_fake_delay_total_ms(void) {
    return s_delay_total_ms;
}

uint32_t esp_fake_ota_begin_count(void) {
    return s_ota_begin_count;
}

uint32_t esp_fake_ota_open_sessions(void) {
    return s_ota_open;
}

uint32_t esp_fake_ota_written(void) {
    return s_ota_written;
}

bool esp_fake_ota_finalised(void) {
    return s_ota_finalised;
}

bool esp_fake_ota_aborted(void) {
    return s_ota_aborted;
}

uint32_t esp_fake_ota_crc(void) {
    return s_ota_crc;
}

void esp_fake_fail_ota_begin(esp_err_t err) {
    s_fail_ota_begin = err;
}

void esp_fake_fail_ota_write(esp_err_t err) {
    s_fail_ota_write = err;
}

void esp_fake_fail_ota_end(esp_err_t err) {
    s_fail_ota_end = err;
}

/* --------------------------- The fakes proper -------------------------- */

const esp_partition_t *esp_partition_find_first(esp_partition_type_t type,
                                                esp_partition_subtype_t subtype,
                                                const char *label) {
    (void)label;

    for (size_t i = 0; i < SLOT_COUNT; ++i) {
        if (!s_slots[i].present) {
            continue;
        }
        if ((s_slots[i].part.type == type) && (s_slots[i].part.subtype == subtype)) {
            return &s_slots[i].part;
        }
    }
    return NULL;
}

const esp_app_desc_t *esp_app_get_description(void) {
    return &s_self;
}

const esp_partition_t *esp_ota_get_running_partition(void) {
    const esp_partition_t *part =
        esp_partition_find_first(ESP_PARTITION_TYPE_APP, s_running, NULL);

    /* On target this never returns NULL, so neither does the fake: a test that
     * wants "no running partition" would be testing a state the chip cannot be
     * in. Falling back to slot 0 keeps that impossibility out of the tests. */
    return (part != NULL) ? part : &s_slots[0].part;
}

esp_err_t esp_ota_get_partition_description(const esp_partition_t *partition,
                                            esp_app_desc_t *out_desc) {
    if ((partition == NULL) || (out_desc == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    /* The running slot answers from the image header, like the target does. */
    if (partition->subtype == s_running) {
        *out_desc = s_self;
        return ESP_OK;
    }

    for (size_t i = 0; i < SLOT_COUNT; ++i) {
        if (s_slots[i].part.subtype != partition->subtype) {
            continue;
        }
        if (!s_slots[i].has_image) {
            return ESP_ERR_NOT_FOUND;
        }
        memset(out_desc, 0, sizeof(*out_desc));
        (void)strncpy(out_desc->version, s_slots[i].version, sizeof(out_desc->version) - 1U);
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t esp_ota_set_boot_partition(const esp_partition_t *partition) {
    if (partition == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_fail_set_boot != ESP_OK) {
        return s_fail_set_boot;
    }
    s_armed = partition->subtype;
    return ESP_OK;
}

esp_err_t esp_ota_begin(const esp_partition_t *partition, size_t image_size,
                        esp_ota_handle_t *out_handle) {
    if ((partition == NULL) || (out_handle == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_fail_ota_begin != ESP_OK) {
        return s_fail_ota_begin;
    }
    if ((image_size != OTA_SIZE_UNKNOWN) && (image_size > partition->size)) {
        /* The real call refuses this too, but the command layer is supposed to
         * have caught it first - the point of the fake is that a test can tell
         * which of the two did. */
        return ESP_ERR_INVALID_SIZE;
    }

    s_ota_begin_count += 1U;
    s_ota_open += 1U;
    s_ota_written = 0U;
    s_ota_crc     = 0U;
    *out_handle   = s_ota_next_handle;
    s_ota_next_handle += 1U;
    return ESP_OK;
}

esp_err_t esp_ota_write(esp_ota_handle_t handle, const void *data, size_t size) {
    if ((handle == 0U) || (data == NULL) || (size == 0U)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_ota_open == 0U) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_fail_ota_write != ESP_OK) {
        return s_fail_ota_write;
    }

    /* Folded rather than stored: the tests care that the right bytes arrived
     * in the right order, not about keeping 13 MB of them around. */
    s_ota_crc = esp_rom_crc32_le(s_ota_crc, (const uint8_t *)data, (uint32_t)size);
    s_ota_written += (uint32_t)size;
    return ESP_OK;
}

esp_err_t esp_ota_end(esp_ota_handle_t handle) {
    if (handle == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_ota_open == 0U) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_fail_ota_end != ESP_OK) {
        /* The real call frees the session even when validation fails. */
        s_ota_open -= 1U;
        return s_fail_ota_end;
    }

    s_ota_open -= 1U;
    s_ota_finalised = true;
    return ESP_OK;
}

esp_err_t esp_ota_abort(esp_ota_handle_t handle) {
    if (handle == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_ota_open == 0U) {
        return ESP_ERR_INVALID_STATE;
    }
    s_ota_open -= 1U;
    s_ota_aborted = true;
    return ESP_OK;
}

esp_err_t esp_read_mac(uint8_t *mac, esp_mac_type_t type) {
    if (mac == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_fail_read_mac != ESP_OK) {
        return s_fail_read_mac;
    }
    if ((size_t)type >= (sizeof(s_mac) / sizeof(s_mac[0]))) {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(mac, s_mac[type], 6U);
    return ESP_OK;
}

void esp_restart(void) {
    s_restart_count += 1U;
}

void vTaskDelay(TickType_t ticks) {
    s_delay_total_ms += (uint32_t)ticks;
}

/* -------------------------- Private functions -------------------------- */

static slot_t *slot_of(esp_partition_subtype_t subtype) {
    for (size_t i = 0; i < SLOT_COUNT; ++i) {
        if (s_slots[i].part.subtype == subtype) {
            return &s_slots[i];
        }
    }
    return NULL;
}

/*** end of file ***/
