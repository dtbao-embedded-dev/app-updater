/**
 * @file    esp_fake.c
 * @date    2026-09-06
 * @brief   State behind the ESP-IDF host fakes (R-TST-07).
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

/* ------------------------------ Includes ------------------------------- */

#include "esp_fake.h"

#include "esp_err.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

/* ----------------------------- Static data ----------------------------- */

static uint8_t s_mac[3][6];
static esp_err_t s_fail_read_mac;
static uint32_t s_restart_count;
static uint32_t s_delay_total_ms;

/* -------------------------- Public functions --------------------------- */

void esp_fake_reset(void) {
    memset(s_mac, 0, sizeof(s_mac));
    s_mac[ESP_MAC_WIFI_STA][0] = 0xAAU;
    s_mac[ESP_MAC_WIFI_STA][5] = 0x01U;
    s_mac[ESP_MAC_BT][0]       = 0xAAU;
    s_mac[ESP_MAC_BT][5]       = 0x02U;

    s_fail_read_mac  = ESP_OK;
    s_restart_count  = 0U;
    s_delay_total_ms = 0U;
}

void esp_fake_set_mac(esp_mac_type_t type, const uint8_t *mac) {
    if ((mac != NULL) && ((size_t)type < (sizeof(s_mac) / sizeof(s_mac[0])))) {
        memcpy(s_mac[type], mac, 6U);
    }
}

void esp_fake_fail_read_mac(esp_err_t err) {
    s_fail_read_mac = err;
}

uint32_t esp_fake_restart_count(void) {
    return s_restart_count;
}

uint32_t esp_fake_delay_total_ms(void) {
    return s_delay_total_ms;
}

/* --------------------------- The fakes proper -------------------------- */

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

/*** end of file ***/
