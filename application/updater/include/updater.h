/**
 * @file    updater.h
 * @date    2026-09-06
 * @brief   Decides when to check for a new image, drives the download, and
 *          confirms or rolls back the result.
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

#ifndef UPDATER_H
#define UPDATER_H

/* ------------------------------ Includes ------------------------------- */

#include <stdbool.h>
#include <stdint.h>

#include "fw.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------- Constants & macros ------------------------- */

/** Consecutive download failures before the updater stops trying until reboot. */
#define UPDATER_FAIL_LIMIT 3U

/* -------------------------------- Types -------------------------------- */

/**
 * @brief   Where the update cycle currently is.
 *
 * The application owns this machine; no module below it runs one of its own
 * (R-LAY-06).
 */
typedef enum {
    UPDATER_STATE_IDLE = 0,     /**< Waiting for the check interval to elapse. */
    UPDATER_STATE_CHECKING,     /**< Asking the server what the latest image is.*/
    UPDATER_STATE_DOWNLOADING,  /**< Writing a new image into the spare slot.  */
    UPDATER_STATE_PENDING_BOOT, /**< Image written; a reboot will run it.      */
    UPDATER_STATE_FAILED,       /**< Gave up after UPDATER_FAIL_LIMIT tries.   */
} updater_state_t;

/**
 * @brief   Called after every state change, for whoever shows progress.
 * @param   ctx    the `on_state_ctx` supplied at init
 * @param   state  the state just entered
 * @note    Runs in the task that called `updater_step()`, never in an ISR.
 *          Must not call back into the updater.
 */
typedef void (*updater_state_cb_t)(void *ctx, updater_state_t state);

/** @brief Configuration for one updater instance. */
typedef struct {
    uint32_t check_interval_ms;  /**< Between checks, 0 .. 2^31-1 ms; 0
                                  *   disables checking entirely.          */
    uint32_t retry_backoff_ms;   /**< After a failed attempt, 0 .. 2^31-1 ms.*/
    updater_state_cb_t on_state; /**< Optional; NULL means no notification. */
    void *on_state_ctx;          /**< Passed back to `on_state` unchanged.  */
} updater_cfg_t;

/** @brief Updater instance. Caller allocates; the module owns the contents. */
typedef struct {
    bool is_init;          /**< Lifecycle state, checked by operations.  */
    bool is_running;       /**< Between `start()` and `stop()`.          */
    updater_cfg_t cfg;     /**< Copy of the config.                      */
    updater_state_t state; /**< Current state of the cycle.              */
    uint32_t next_due_ms;  /**< `now_ms` at which the next check is due. */
    uint32_t fail_count;   /**< Consecutive failed attempts.             */
} updater_t;

/* ------------------------ Public function prototypes ------------------- */

/**
 * @brief   Returns a short, stable name for a state.
 * @param   state   any `updater_state_t`; unknown yields "UPDATER_STATE_UNKNOWN"
 * @return  Pointer to a string literal with static lifetime; never NULL.
 * @note    Reentrant, any task.
 */
const char *updater_state_str(updater_state_t state);

/**
 * @brief   Fills a config with the defaults this build was compiled with.
 * @return  By value; every field is set.
 * @note    Reentrant, any task.
 */
updater_cfg_t updater_cfg_default(void);

/**
 * @brief   Validates the config and puts the cycle in IDLE. No traffic.
 * @param   up    caller-allocated instance, zeroed by this call
 * @param   cfg   copied into the instance
 * @return  FW_OK, FW_ERR_PARAM on a NULL argument or an interval at or past
 *          2^31 ms (~24.8 days, the scheduling horizon), FW_ERR_STATE when
 *          already initialized.
 * @note    One caller only (R-LFC-09: nothing runs until `updater_start()`).
 */
fw_err_t updater_init(updater_t *up, const updater_cfg_t *cfg);

/**
 * @brief   Arms the cycle, scheduling the first check one interval out.
 * @param   up      initialized instance
 * @param   now_ms  the caller's monotonic millisecond clock
 * @return  FW_OK, FW_ERR_PARAM on NULL, FW_ERR_STATE before init or when
 *          already running.
 * @note    One caller only.
 */
fw_err_t updater_start(updater_t *up, uint32_t now_ms);

/**
 * @brief   Disarms the cycle and returns it to IDLE.
 * @param   up   instance, possibly only partly initialized
 * @return  FW_OK, or FW_ERR_PARAM when `up` is NULL.
 * @note    Repeatable and safe on a partly built instance (R-LFC-04). Does not
 *          abort a download already in flight; it stops the next one starting.
 */
fw_err_t updater_stop(updater_t *up);

/**
 * @brief   Releases everything `updater_init()` claimed.
 * @param   up   instance, possibly only partly initialized
 * @return  FW_OK, or FW_ERR_PARAM when `up` is NULL.
 * @note    Repeatable and safe on a partly built instance (R-LFC-04).
 */
fw_err_t updater_deinit(updater_t *up);

/**
 * @brief   Advances the cycle by one step. Call it from the application loop.
 * @param   up      running instance
 * @param   now_ms  monotonic millisecond clock, supplied by the caller so this
 *                  function can be exercised on a host (R-TST-06). It must
 *                  never go backwards; wrap-around at 2^32 is handled.
 * @return  FW_OK when the step completed, FW_ERR_PARAM on NULL, FW_ERR_STATE
 *          before `updater_start()`, or the failure the step hit.
 * @note    Blocks for as long as the work it starts. One caller only; not
 *          callable from an ISR.
 */
fw_err_t updater_step(updater_t *up, uint32_t now_ms);

/**
 * @brief   Reports the current state.
 * @param   up            initialized instance
 * @param[out] out_state  the state at the moment of the call
 * @return  FW_OK, FW_ERR_PARAM on NULL, FW_ERR_STATE before init.
 * @note    Readable from another task; the value may be stale on return.
 */
fw_err_t updater_state_get(const updater_t *up, updater_state_t *out_state);

#ifdef __cplusplus
}
#endif

#endif /* UPDATER_H */

/*** end of file ***/
