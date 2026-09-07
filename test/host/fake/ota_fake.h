/**
 * @file    ota_fake.h
 * @date    2026-09-07
 * @brief   Test-side control surface for the host fake of `driver/ota`.
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

#ifndef HOST_FAKE_OTA_FAKE_H
#define HOST_FAKE_OTA_FAKE_H

/* ------------------------------ Includes ------------------------------- */

#include "ota.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------ Public function prototypes ------------------- */

/*
 * A fake of the driver, not of the vendor SDK. The module under test calls
 * `ota_*` and this file answers, so the test drives the same contract the
 * target implementation has to satisfy — a stub of `esp_ota_ops.h` would
 * instead pin the test to one platform, which is the defect the driver exists
 * to remove.
 *
 * State lives here rather than as `static` data in the header, because a
 * header's static would give the module under test and the test their own
 * private copy each, and then a test could never see what the module did.
 *
 * Every test calls ota_fake_reset() first (R-TST-05): this is global state,
 * and a test that inherits the previous test's forced failure is a test that
 * passes for the wrong reason.
 */

/** @brief Puts the fake back to a plausible, unforced default. */
void ota_fake_reset(void);

/** @brief Sets which slot is reported as running. */
void ota_fake_set_running_slot(uint8_t slot);

/** @brief Sets the version string of the image reported as running. */
void ota_fake_set_running_version(const char *version);

/**
 * @brief   Sets a slot's stored image version.
 * @param   version   NULL means the slot holds no readable image, so
 *                    `ota_slot_version_get()` answers OTA_ERR_NOT_FOUND.
 */
void ota_fake_set_slot_version(uint8_t slot, const char *version);

/** @brief Sets a slot's size, so a test can make an image not fit. */
void ota_fake_set_slot_size(uint8_t slot, uint32_t size);

/** @brief Makes a slot absent from the partition table entirely. */
void ota_fake_remove_slot(uint8_t slot);

/** @brief Sets whether this boot is the one trial boot of a new image. */
void ota_fake_set_pending_verify(bool pending);

/** @brief Forces the matching call to fail; OTA_OK clears it. */
void ota_fake_fail_boot_slot_set(ota_err_t err);
void ota_fake_fail_begin(ota_err_t err);
void ota_fake_fail_write(ota_err_t err);
void ota_fake_fail_end(ota_err_t err);

/** @brief Slot last armed by `ota_boot_slot_set()`, or -1 when none was. */
int ota_fake_boot_slot_armed(void);

/** @brief True once `ota_mark_valid()` has confirmed the running image. */
bool ota_fake_marked_valid(void);

/**
 * @brief   How many times a session was opened, so a rejected request can be
 *          proved to have erased nothing.
 */
uint32_t ota_fake_begin_count(void);

/** @brief Sessions opened but neither ended nor aborted. Must return to 0. */
uint32_t ota_fake_open_sessions(void);

/** @brief Bytes handed to `ota_session_write()` in the current session. */
uint32_t ota_fake_written(void);

/** @brief True once `ota_session_end()` has finalised a slot. */
bool ota_fake_finalised(void);

/** @brief True once a session was thrown away by `ota_session_abort()`. */
bool ota_fake_aborted(void);

/** @brief CRC-32 of every byte written, to check what actually landed. */
uint32_t ota_fake_crc(void);

#ifdef __cplusplus
}
#endif

#endif /* HOST_FAKE_OTA_FAKE_H */

/*** end of file ***/
