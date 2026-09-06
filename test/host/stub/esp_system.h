/**
 * @file    esp_system.h
 * @date    2026-09-06
 * @brief   Host fake for the ESP-IDF system header (R-TST-07).
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

#ifndef HOST_STUB_ESP_SYSTEM_H
#define HOST_STUB_ESP_SYSTEM_H

/* ------------------------------ Includes ------------------------------- */

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------ Public function prototypes ------------------- */

/**
 * @brief   Counts a reset instead of performing one.
 * @note    This is THE fake that makes the RESTART_APP ordering testable. On
 *          target the call never returns, so a host test is the only place the
 *          "reply first, then reboot" contract can be checked at all: the test
 *          asserts the reply was captured before the count went up.
 */
void esp_restart(void);

#ifdef __cplusplus
}
#endif

#endif /* HOST_STUB_ESP_SYSTEM_H */

/*** end of file ***/
