/**
 * @file    FreeRTOS.h
 * @date    2026-09-06
 * @brief   Host fake for the FreeRTOS kernel header (R-TST-07).
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

#ifndef HOST_STUB_FREERTOS_H
#define HOST_STUB_FREERTOS_H

/* ------------------------------ Includes ------------------------------- */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------- Constants & macros ------------------------- */

/* The tick rate does not matter here - nothing on the host waits - but the
 * conversion has to compile and produce something a delay can take. */
#define configTICK_RATE_HZ 1000U

#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))

/* -------------------------------- Types -------------------------------- */

typedef uint32_t TickType_t;

#ifdef __cplusplus
}
#endif

#endif /* HOST_STUB_FREERTOS_H */

/*** end of file ***/
