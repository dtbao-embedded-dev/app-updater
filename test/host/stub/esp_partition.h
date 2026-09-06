/**
 * @file    esp_partition.h
 * @date    2026-09-06
 * @brief   Host fake for the ESP-IDF partition header (R-TST-07).
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

#ifndef HOST_STUB_ESP_PARTITION_H
#define HOST_STUB_ESP_PARTITION_H

/* ------------------------------ Includes ------------------------------- */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------- Types -------------------------------- */

typedef enum {
    ESP_PARTITION_TYPE_APP  = 0x00,
    ESP_PARTITION_TYPE_DATA = 0x01,
} esp_partition_type_t;

typedef enum {
    ESP_PARTITION_SUBTYPE_APP_FACTORY = 0x00,
    ESP_PARTITION_SUBTYPE_APP_OTA_0   = 0x10,
    ESP_PARTITION_SUBTYPE_APP_OTA_1   = 0x11,
    ESP_PARTITION_SUBTYPE_ANY         = 0xFF,
} esp_partition_subtype_t;

/* Only the fields our code reads. A fake that carried the whole vendor struct
 * would invite a test to depend on a field the firmware never looks at. */
typedef struct {
    esp_partition_type_t type;
    esp_partition_subtype_t subtype;
    uint32_t address;
    uint32_t size;
    char label[17];
} esp_partition_t;

/* ------------------------ Public function prototypes ------------------- */

/**
 * @brief   Finds the first partition matching type and subtype.
 * @return  A pointer into the fake table, or NULL when the test said that slot
 *          does not exist. Real linkage, not static inline: the test and the
 *          module under test have to see the same table.
 */
const esp_partition_t *esp_partition_find_first(esp_partition_type_t type,
                                                esp_partition_subtype_t subtype,
                                                const char *label);

#ifdef __cplusplus
}
#endif

#endif /* HOST_STUB_ESP_PARTITION_H */

/*** end of file ***/
