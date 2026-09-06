/**
 * @file    esp_app_desc.h
 * @date    2026-09-06
 * @brief   Host fake for the ESP-IDF app description header (R-TST-07).
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

#ifndef HOST_STUB_ESP_APP_DESC_H
#define HOST_STUB_ESP_APP_DESC_H

/* ------------------------------ Includes ------------------------------- */

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------- Types -------------------------------- */

/* Only `version` is faked: it is the one field the VERSION command reads. */
typedef struct {
    char version[32];
    char project_name[32];
} esp_app_desc_t;

/* ------------------------ Public function prototypes ------------------- */

/** @brief The description of the image running now, per the fake's state. */
const esp_app_desc_t *esp_app_get_description(void);

#ifdef __cplusplus
}
#endif

#endif /* HOST_STUB_ESP_APP_DESC_H */

/*** end of file ***/
