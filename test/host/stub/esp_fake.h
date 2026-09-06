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
#include "esp_partition.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------ Public function prototypes ------------------- */

/*
 * The fakes keep their state in esp_fake.c rather than as `static` data in a
 * header, because a header's static would give the module under test and the
 * test its own private copy each - and then a test could never see what the
 * module did. Real linkage is what makes these observable.
 *
 * Every test calls esp_fake_reset() first (R-TST-05): the fakes are the only
 * global state in the suite, and a test that inherits the previous test's
 * forced failure is a test that passes for the wrong reason.
 */

/** @brief Puts every fake back to a plausible, unforced default. */
void esp_fake_reset(void);

/** @brief Sets which app slot is reported as running. */
void esp_fake_set_running_slot(esp_partition_subtype_t subtype);

/** @brief Sets the version string of the image reported as running. */
void esp_fake_set_self_version(const char *version);

/**
 * @brief   Sets a slot's stored image version.
 * @param   version   NULL means the slot holds no valid image, so
 *                    `esp_ota_get_partition_description()` fails for it.
 */
void esp_fake_set_slot_version(esp_partition_subtype_t subtype, const char *version);

/** @brief Sets a slot's size, so a test can make an image not fit. */
void esp_fake_set_slot_size(esp_partition_subtype_t subtype, uint32_t size);

/** @brief Makes a slot absent from the partition table entirely. */
void esp_fake_remove_slot(esp_partition_subtype_t subtype);

/** @brief Sets the MAC a given type reads back. */
void esp_fake_set_mac(esp_mac_type_t type, const uint8_t *mac);

/** @brief Forces `esp_read_mac()` to fail with this code; ESP_OK clears it. */
void esp_fake_fail_read_mac(esp_err_t err);

/** @brief Forces `esp_ota_set_boot_partition()` to fail; ESP_OK clears it. */
void esp_fake_fail_set_boot(esp_err_t err);

/** @brief How many times `esp_restart()` has been called. */
uint32_t esp_fake_restart_count(void);

/** @brief Subtype last armed by `esp_ota_set_boot_partition()`, or 0 if none. */
esp_partition_subtype_t esp_fake_boot_slot_armed(void);

/** @brief Total milliseconds handed to `vTaskDelay()`. */
uint32_t esp_fake_delay_total_ms(void);

/* --- the OTA write session ------------------------------------------------ */

/** @brief How many times a session was opened, so a rejected request can be
 *         proved to have erased nothing. */
uint32_t esp_fake_ota_begin_count(void);

/** @brief Sessions opened but neither ended nor aborted. Must return to 0. */
uint32_t esp_fake_ota_open_sessions(void);

/** @brief Bytes handed to `esp_ota_write()` in the current session. */
uint32_t esp_fake_ota_written(void);

/** @brief True once `esp_ota_end()` has marked a slot valid. */
bool esp_fake_ota_finalised(void);

/** @brief True once a session was thrown away by `esp_ota_abort()`. */
bool esp_fake_ota_aborted(void);

/** @brief CRC-32 of every byte written, to check what actually landed. */
uint32_t esp_fake_ota_crc(void);

/** @brief Forces the matching call to fail; ESP_OK clears it. */
void esp_fake_fail_ota_begin(esp_err_t err);
void esp_fake_fail_ota_write(esp_err_t err);
void esp_fake_fail_ota_end(esp_err_t err);

#ifdef __cplusplus
}
#endif

#endif /* HOST_STUB_ESP_FAKE_H */

/*** end of file ***/
