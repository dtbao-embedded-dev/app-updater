/**
 * @file    esp_err.h
 * @date    2026-09-06
 * @brief   Host fake for the ESP-IDF error header (R-TST-07).
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

#ifndef HOST_STUB_ESP_ERR_H
#define HOST_STUB_ESP_ERR_H

/* ------------------------------ Includes ------------------------------- */

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------- Constants & macros ------------------------- */

/* Only the codes our modules actually convert in their from_esp_err(). The
 * numbers match ESP-IDF's so a test can state the vendor value it is faking
 * and mean the same thing the target would. */
#define ESP_OK                    0
#define ESP_FAIL                  (-1)
#define ESP_ERR_NO_MEM            0x101
#define ESP_ERR_INVALID_ARG       0x102
#define ESP_ERR_INVALID_STATE     0x103
#define ESP_ERR_INVALID_SIZE      0x104
#define ESP_ERR_NOT_FOUND         0x105
#define ESP_ERR_TIMEOUT           0x107
#define ESP_ERR_OTA_VALIDATE_FAILED 0x1503

/* -------------------------------- Types -------------------------------- */

typedef int esp_err_t;

#ifdef __cplusplus
}
#endif

#endif /* HOST_STUB_ESP_ERR_H */

/*** end of file ***/
