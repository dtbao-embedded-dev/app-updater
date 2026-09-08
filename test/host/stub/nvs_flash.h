/**
 * @file    nvs_flash.h
 * @date    2026-09-07
 * @brief   Host stub for the ESP-IDF NVS subsystem header (R-TST-07).
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

#ifndef HOST_STUB_NVS_FLASH_H
#define HOST_STUB_NVS_FLASH_H

/* ------------------------------ Includes ------------------------------- */

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------ Public function prototypes ------------------- */

esp_err_t nvs_flash_init(void);
esp_err_t nvs_flash_erase(void);

#ifdef __cplusplus
}
#endif

#endif /* HOST_STUB_NVS_FLASH_H */

/*** end of file ***/
