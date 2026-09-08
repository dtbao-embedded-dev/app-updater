/**
 * @file    app.h
 * @date    2026-09-06
 * @brief   Brings every module up in order and runs the update cycle.
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

#ifndef APP_H
#define APP_H

/* ------------------------------ Includes ------------------------------- */

#include "fw.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------- Constants & macros ------------------------- */

/** How often the update cycle is stepped. Shorter costs CPU, longer delays a
 *  state change the operator is watching for. */
#define APP_TICK_MS 1000U

/* -------------------------------- Types -------------------------------- */

/* ------------------------ Public function prototypes ------------------- */

/**
 * @brief   Initializes every module, then runs the update loop forever.
 * @return  Only on a failure to bring the stack up: FW_ERR_IO when NVS or the
 *          BSP refused, FW_ERR_PARAM on a config the build got wrong.
 * @note    Runs in the caller task and does not return on success. One caller
 *          only, which is app_main().
 */
fw_err_t app_run(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_H */

/*** end of file ***/
