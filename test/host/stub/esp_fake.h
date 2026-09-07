/**
 * @file    esp_fake.h
 * @date    2026-09-06
 * @brief   Test-side control surface for the ESP-IDF host fakes (R-TST-07).
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

#ifndef HOST_STUB_ESP_FAKE_H
#define HOST_STUB_ESP_FAKE_H

/* ------------------------------ Includes ------------------------------- */

#include "esp_err.h"
#include "esp_mac.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------ Public function prototypes ------------------- */

/*
 * What is left here is the SDK surface middleware still reaches for directly:
 * the eFuse MAC, the reset, and the tick delay. Everything about OTA slots
 * moved to `fake/ota_fake.c`, which fakes OUR driver contract instead of a
 * vendor header - see the note at the top of test/host/CMakeLists.txt.
 *
 * The fakes keep their state in esp_fake.c rather than as `static` data in a
 * header, because a header's static would give the module under test and the
 * test its own private copy each - and then a test could never see what the
 * module did. Real linkage is what makes these observable.
 *
 * Every test calls esp_fake_reset() first (R-TST-05): the fakes are global
 * state, and a test that inherits the previous test's forced failure is a test
 * that passes for the wrong reason.
 */

/** @brief Puts every fake back to a plausible, unforced default. */
void esp_fake_reset(void);

/** @brief Sets the MAC a given type reads back. */
void esp_fake_set_mac(esp_mac_type_t type, const uint8_t *mac);

/** @brief Forces `esp_read_mac()` to fail with this code; ESP_OK clears it. */
void esp_fake_fail_read_mac(esp_err_t err);

/** @brief How many times `esp_restart()` has been called. */
uint32_t esp_fake_restart_count(void);

/** @brief Total milliseconds handed to `vTaskDelay()`. */
uint32_t esp_fake_delay_total_ms(void);

#ifdef __cplusplus
}
#endif

#endif /* HOST_STUB_ESP_FAKE_H */

/*** end of file ***/
