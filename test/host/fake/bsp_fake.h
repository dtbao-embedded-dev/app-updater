/**
 * @file    bsp_fake.h
 * @date    2026-09-07
 * @brief   Test-side control surface for the host fake of `driver/bsp`.
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

#ifndef HOST_FAKE_BSP_FAKE_H
#define HOST_FAKE_BSP_FAKE_H

/* ------------------------------ Includes ------------------------------- */

#include "bsp.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------ Public function prototypes ------------------- */

/*
 * Only the instance-free half of `bsp.h` is faked here - the eFuse MAC and the
 * reset - because that is the whole of the BSP that middleware calls. Nothing
 * host-built touches `bsp_init()` or the LED, so those are absent rather than
 * stubbed: a fake of a function nobody calls is a claim nobody checks.
 *
 * Every test calls bsp_fake_reset() first (R-TST-05).
 */

/** @brief Puts the fake back to a plausible, unforced default. */
void bsp_fake_reset(void);

/** @brief Sets the MAC a given kind reads back. */
void bsp_fake_set_mac(bsp_mac_kind_t kind, const uint8_t *mac);

/** @brief Forces `bsp_mac_get()` to fail with this code; BSP_OK clears it. */
void bsp_fake_fail_mac_get(bsp_err_t err);

/** @brief How many times `bsp_restart()` has been called. */
uint32_t bsp_fake_restart_count(void);

/** @brief Total grace milliseconds handed to `bsp_restart()`. */
uint32_t bsp_fake_grace_total_ms(void);

#ifdef __cplusplus
}
#endif

#endif /* HOST_FAKE_BSP_FAKE_H */

/*** end of file ***/
