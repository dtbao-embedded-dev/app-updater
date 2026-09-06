/**
 * @file    task.h
 * @date    2026-09-06
 * @brief   Host fake for the FreeRTOS task header (R-TST-07).
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

#ifndef HOST_STUB_FREERTOS_TASK_H
#define HOST_STUB_FREERTOS_TASK_H

/* ------------------------------ Includes ------------------------------- */

#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------ Public function prototypes ------------------- */

/**
 * @brief   Records a delay instead of sleeping.
 * @note    A host suite that really slept would take as long as the firmware
 *          waits, for no extra coverage. The recorded total is readable
 *          through `esp_fake_delay_total_ms()`, which is how the RESTART_APP
 *          test proves the reply is given time to leave before the reset.
 */
void vTaskDelay(TickType_t ticks);

#ifdef __cplusplus
}
#endif

#endif /* HOST_STUB_FREERTOS_TASK_H */

/*** end of file ***/
