/**
 * @file    esp_mac.h
 * @date    2026-09-06
 * @brief   Host fake for the ESP-IDF MAC address header (R-TST-07).
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

#ifndef HOST_STUB_ESP_MAC_H
#define HOST_STUB_ESP_MAC_H

/* ------------------------------ Includes ------------------------------- */

#include "esp_err.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------- Types -------------------------------- */

typedef enum {
    ESP_MAC_WIFI_STA = 0,
    ESP_MAC_BT       = 2,
} esp_mac_type_t;

/* ------------------------ Public function prototypes ------------------- */

/**
 * @brief   Reads a burned-in MAC address.
 * @return  ESP_OK with the six bytes the test set, or the error it forced.
 * @note    On target this reads eFuse, so it works with no radio brought up -
 *          which is why this product can answer WIFI_MAC and BLE_MAC at all.
 */
esp_err_t esp_read_mac(uint8_t *mac, esp_mac_type_t type);

#ifdef __cplusplus
}
#endif

#endif /* HOST_STUB_ESP_MAC_H */

/*** end of file ***/
