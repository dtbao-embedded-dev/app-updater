/**
 * @file    app_priv.h
 * @author  dtbao
 * @date    2026-09-06
 * @brief   Declarations internal to the app module; never installed.
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

#ifndef APP_PRIV_H
#define APP_PRIV_H

/* ------------------------------ Includes ------------------------------- */

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------ Public function prototypes ------------------- */

/**
 * @brief   ESP-IDF entry point. The name is fixed by the SDK.
 * @note    Declared here because ESP-IDF publishes no prototype for it and
 *          -Wmissing-prototypes (R-BLD-01) would otherwise fail the build.
 *          Runs in the main task; it must not return.
 */
void app_main(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_PRIV_H */

/*** end of file ***/
