/**
 * @file    updater.c
 * @date    2026-09-06
 * @brief   Decides when to check for a new image, drives the download, and
 *          confirms or rolls back the result.
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

/* ------------------------------ Includes ------------------------------- */

#include "updater.h"

#include "esp_log.h"

#include <string.h>

/* --------------------------- Private macros ---------------------------- */

/** Six hours between checks, and a minute before retrying a failure. */
#define UPDATER_DEFAULT_CHECK_MS   (6U * 60U * 60U * 1000U)
#define UPDATER_DEFAULT_BACKOFF_MS (60U * 1000U)

/** Longest schedulable delay: half the uint32 millisecond range (~24.8 days).
 *  Past this, a wrapped difference is indistinguishable from "already due". */
#define UPDATER_HORIZON_MS 0x80000000U

/* ---------------------------- Private types ---------------------------- */

/* ----------------------------- Static data ----------------------------- */

static const char *TAG = "updater";

/* --------------------- Private function prototypes --------------------- */

static void enter_state(updater_t *up, updater_state_t next);
static bool is_due(const updater_t *up, uint32_t now_ms);
static fw_err_t step_checking(updater_t *up, uint32_t now_ms);
static fw_err_t step_downloading(updater_t *up, uint32_t now_ms);
static void note_failure(updater_t *up, uint32_t now_ms);

/* -------------------------- Public functions --------------------------- */

const char *updater_state_str(updater_state_t state) {
    switch (state) {
        case UPDATER_STATE_IDLE:
            return "UPDATER_STATE_IDLE";
        case UPDATER_STATE_CHECKING:
            return "UPDATER_STATE_CHECKING";
        case UPDATER_STATE_DOWNLOADING:
            return "UPDATER_STATE_DOWNLOADING";
        case UPDATER_STATE_PENDING_BOOT:
            return "UPDATER_STATE_PENDING_BOOT";
        case UPDATER_STATE_FAILED:
            return "UPDATER_STATE_FAILED";
    }

    return "UPDATER_STATE_UNKNOWN";
}

updater_cfg_t updater_cfg_default(void) {
    const updater_cfg_t cfg = {
        .check_interval_ms = UPDATER_DEFAULT_CHECK_MS,
        .retry_backoff_ms  = UPDATER_DEFAULT_BACKOFF_MS,
        .on_state          = NULL,
        .on_state_ctx      = NULL,
    };
    return cfg;
}

fw_err_t updater_init(updater_t *up, const updater_cfg_t *cfg) {
    if ((up == NULL) || (cfg == NULL)) {
        return FW_ERR_PARAM;
    }
    if (up->is_init) {
        return FW_ERR_STATE;
    }
    /* is_due() compares a wrapped difference against half the uint32 range, so
     * an interval at or past 2^31 ms (~24.8 days) would read as already due on
     * the first step. Reject it rather than silently checking every loop. */
    if ((cfg->check_interval_ms >= UPDATER_HORIZON_MS) ||
        (cfg->retry_backoff_ms >= UPDATER_HORIZON_MS)) {
        return FW_ERR_PARAM;
    }

    memset(up, 0, sizeof(*up));
    up->cfg     = *cfg;
    up->state   = UPDATER_STATE_IDLE;
    up->is_init = true;
    return FW_OK;
}

fw_err_t updater_start(updater_t *up, uint32_t now_ms) {
    if (up == NULL) {
        return FW_ERR_PARAM;
    }
    if (!up->is_init || up->is_running) {
        return FW_ERR_STATE;
    }

    up->fail_count  = 0U;
    up->next_due_ms = now_ms + up->cfg.check_interval_ms;
    up->is_running  = true;
    enter_state(up, UPDATER_STATE_IDLE);
    return FW_OK;
}

fw_err_t updater_stop(updater_t *up) {
    if (up == NULL) {
        return FW_ERR_PARAM;
    }

    /* Repeatable and safe on a partly built instance (R-LFC-04). */
    up->is_running = false;
    if (up->is_init) {
        enter_state(up, UPDATER_STATE_IDLE);
    }
    return FW_OK;
}

fw_err_t updater_deinit(updater_t *up) {
    if (up == NULL) {
        return FW_ERR_PARAM;
    }

    memset(up, 0, sizeof(*up));
    return FW_OK;
}

fw_err_t updater_step(updater_t *up, uint32_t now_ms) {
    if (up == NULL) {
        return FW_ERR_PARAM;
    }
    if (!up->is_init || !up->is_running) {
        return FW_ERR_STATE;
    }

    switch (up->state) {
        case UPDATER_STATE_IDLE:
            /* A zero interval disables checking entirely (R-CFG-03: the value
             * is a real setting, not a missing one). */
            if ((up->cfg.check_interval_ms != 0U) && is_due(up, now_ms)) {
                enter_state(up, UPDATER_STATE_CHECKING);
            }
            return FW_OK;

        case UPDATER_STATE_CHECKING:
            return step_checking(up, now_ms);

        case UPDATER_STATE_DOWNLOADING:
            return step_downloading(up, now_ms);

        case UPDATER_STATE_PENDING_BOOT:
        case UPDATER_STATE_FAILED:
            /* Both are terminal until the application reboots or restarts the
             * cycle. Stepping them is legal and does nothing. */
            return FW_OK;
    }

    return FW_ERR_STATE;
}

fw_err_t updater_state_get(const updater_t *up, updater_state_t *out_state) {
    if ((up == NULL) || (out_state == NULL)) {
        return FW_ERR_PARAM;
    }
    if (!up->is_init) {
        return FW_ERR_STATE;
    }

    *out_state = up->state;
    return FW_OK;
}

/* -------------------------- Private functions -------------------------- */

/* Arguments are checked at the public boundary (R-SRC-06); helpers assume it. */

static void enter_state(updater_t *up, updater_state_t next) {
    if (up->state == next) {
        return;
    }

    ESP_LOGI(TAG, "%s -> %s (fails=%lu)", updater_state_str(up->state), updater_state_str(next),
             (unsigned long)up->fail_count);
    up->state = next;

    if (up->cfg.on_state != NULL) {
        up->cfg.on_state(up->cfg.on_state_ctx, next);
    }
}

/* Unsigned wrap-around is defined, so the subtraction is correct across the
 * 2^32 ms (~49.7 day) boundary; comparing the timestamps directly is not. */
static bool is_due(const updater_t *up, uint32_t now_ms) {
    return (uint32_t)(now_ms - up->next_due_ms) < UPDATER_HORIZON_MS;
}

static fw_err_t step_checking(updater_t *up, uint32_t now_ms) {
    /* TODO(dtbao): fetch the manifest through ota_http, compare its version
     * against esp_app_get_description()->version, and go to DOWNLOADING only
     * when it is newer. Until that exists the check finds nothing and the
     * cycle re-arms, which is the safe direction to be wrong in. */
    up->next_due_ms = now_ms + up->cfg.check_interval_ms;
    enter_state(up, UPDATER_STATE_IDLE);
    return FW_OK;
}

static fw_err_t step_downloading(updater_t *up, uint32_t now_ms) {
    /* TODO(dtbao): drive ota_http_fetch() into ota_session_write(), then
     * ota_boot_slot_set() and enter PENDING_BOOT. The USB path already
     * runs those session rules in middleware/command; reuse them rather
     * than growing a second set. Never esp_ota_* directly - see
     * docs/memory-ai/rule/layer-boundaries.md. */
    note_failure(up, now_ms);
    return FW_ERR_UNSUPPORTED;
}

static void note_failure(updater_t *up, uint32_t now_ms) {
    up->fail_count += 1U;
    if (up->fail_count >= UPDATER_FAIL_LIMIT) {
        ESP_LOGE(TAG, "giving up after %lu attempts", (unsigned long)up->fail_count);
        enter_state(up, UPDATER_STATE_FAILED);
        return;
    }

    up->next_due_ms = now_ms + up->cfg.retry_backoff_ms;
    enter_state(up, UPDATER_STATE_IDLE);
}

/*** end of file ***/
