/**
 * @file    test_updater.c
 * @author  dtbao
 * @date    2026-09-06
 * @brief   Host tests for the update cycle's scheduling and state guards.
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

/* ------------------------------ Includes ------------------------------- */

#include "unity.h"

#include "updater.h"

/* --------------------------- Private macros ---------------------------- */

#define TEST_INTERVAL_MS 1000U
#define TEST_BACKOFF_MS  100U

/* --------------------- Private function prototypes --------------------- */

static updater_cfg_t test_cfg(void);
static updater_state_t state_of(const updater_t *up);

/* ------------------------ Public function prototypes ------------------- */

/* The runner in test/host/runner.c calls these. Declaring them here is what
 * satisfies -Wmissing-prototypes under the house warning set (R-BLD-01) --
 * a test function is an external symbol like any other. */
void test_updater_step_before_start_returns_err_state(void);
void test_updater_init_twice_returns_err_state(void);
void test_updater_rejects_null_arguments(void);
void test_updater_init_rejects_an_interval_past_the_horizon(void);
void test_updater_stays_idle_until_the_interval_elapses(void);
void test_updater_fires_across_the_millisecond_wrap(void);
void test_updater_rearms_after_a_check_finds_nothing(void);
void test_updater_never_checks_when_the_interval_is_zero(void);
void test_updater_stop_and_deinit_are_repeatable(void);
void test_updater_state_str_names_every_state(void);

/* -------------------------- Public functions --------------------------- */

/* R-LFC-05: an operation called in the wrong state returns ERR_STATE. */
void test_updater_step_before_start_returns_err_state(void) {
    updater_t up            = {0};
    const updater_cfg_t cfg = test_cfg();

    TEST_ASSERT_EQUAL_INT(FW_ERR_STATE, updater_step(&up, 0U));
    TEST_ASSERT_EQUAL_INT(FW_OK, updater_init(&up, &cfg));
    TEST_ASSERT_EQUAL_INT(FW_ERR_STATE, updater_step(&up, 0U));
}

void test_updater_init_twice_returns_err_state(void) {
    updater_t up            = {0};
    const updater_cfg_t cfg = test_cfg();

    TEST_ASSERT_EQUAL_INT(FW_OK, updater_init(&up, &cfg));
    TEST_ASSERT_EQUAL_INT(FW_ERR_STATE, updater_init(&up, &cfg));
}

/* R-SRC-06: a NULL argument is rejected before anything changes. */
void test_updater_rejects_null_arguments(void) {
    updater_t up            = {0};
    const updater_cfg_t cfg = test_cfg();
    updater_state_t state   = UPDATER_STATE_FAILED;

    TEST_ASSERT_EQUAL_INT(FW_ERR_PARAM, updater_init(NULL, &cfg));
    TEST_ASSERT_EQUAL_INT(FW_ERR_PARAM, updater_init(&up, NULL));
    TEST_ASSERT_EQUAL_INT(FW_ERR_PARAM, updater_step(NULL, 0U));
    TEST_ASSERT_EQUAL_INT(FW_ERR_PARAM, updater_state_get(NULL, &state));
    TEST_ASSERT_EQUAL_INT(FW_ERR_PARAM, updater_state_get(&up, NULL));
}

/* An interval past the wrap horizon would read as "already due" forever. */
void test_updater_init_rejects_an_interval_past_the_horizon(void) {
    updater_t up      = {0};
    updater_cfg_t cfg = test_cfg();

    cfg.check_interval_ms = 0x80000000U;
    TEST_ASSERT_EQUAL_INT(FW_ERR_PARAM, updater_init(&up, &cfg));

    cfg                  = test_cfg();
    cfg.retry_backoff_ms = 0xFFFFFFFFU;
    TEST_ASSERT_EQUAL_INT(FW_ERR_PARAM, updater_init(&up, &cfg));
}

/* The cycle stays IDLE until the interval has actually elapsed. */
void test_updater_stays_idle_until_the_interval_elapses(void) {
    updater_t up            = {0};
    const updater_cfg_t cfg = test_cfg();

    TEST_ASSERT_EQUAL_INT(FW_OK, updater_init(&up, &cfg));
    TEST_ASSERT_EQUAL_INT(FW_OK, updater_start(&up, 0U));

    TEST_ASSERT_EQUAL_INT(FW_OK, updater_step(&up, TEST_INTERVAL_MS - 1U));
    TEST_ASSERT_EQUAL_INT(UPDATER_STATE_IDLE, state_of(&up));

    TEST_ASSERT_EQUAL_INT(FW_OK, updater_step(&up, TEST_INTERVAL_MS));
    TEST_ASSERT_EQUAL_INT(UPDATER_STATE_CHECKING, state_of(&up));
}

/* R-TST-06: the clock is injected, so the wrap is testable without waiting
 * 49.7 days for it. A check due just past 2^32 must still fire. */
void test_updater_fires_across_the_millisecond_wrap(void) {
    updater_t up            = {0};
    const updater_cfg_t cfg = test_cfg();
    const uint32_t start_ms = 0xFFFFFF00U; /* 256 ms before the clock wraps */

    TEST_ASSERT_EQUAL_INT(FW_OK, updater_init(&up, &cfg));
    TEST_ASSERT_EQUAL_INT(FW_OK, updater_start(&up, start_ms));

    /* start_ms + 1000 wraps next_due_ms round to 0x000002E8, so for the 1024 ms
     * before the wrap `now_ms` is NUMERICALLY LARGER than the deadline while
     * still being earlier than it. This tick is the one that fails if the due
     * test ever becomes a plain `now_ms >= next_due_ms`. */
    TEST_ASSERT_EQUAL_INT(FW_OK, updater_step(&up, 0xFFFFFFF0U));
    TEST_ASSERT_EQUAL_INT(UPDATER_STATE_IDLE, state_of(&up));

    /* Past the wrap now, but one tick short of the deadline. */
    TEST_ASSERT_EQUAL_INT(FW_OK, updater_step(&up, start_ms + TEST_INTERVAL_MS - 1U));
    TEST_ASSERT_EQUAL_INT(UPDATER_STATE_IDLE, state_of(&up));

    /* On the deadline. */
    TEST_ASSERT_EQUAL_INT(FW_OK, updater_step(&up, start_ms + TEST_INTERVAL_MS));
    TEST_ASSERT_EQUAL_INT(UPDATER_STATE_CHECKING, state_of(&up));
}

/* A check that finds nothing re-arms the timer instead of spinning. */
void test_updater_rearms_after_a_check_finds_nothing(void) {
    updater_t up            = {0};
    const updater_cfg_t cfg = test_cfg();

    TEST_ASSERT_EQUAL_INT(FW_OK, updater_init(&up, &cfg));
    TEST_ASSERT_EQUAL_INT(FW_OK, updater_start(&up, 0U));
    TEST_ASSERT_EQUAL_INT(FW_OK, updater_step(&up, TEST_INTERVAL_MS));
    TEST_ASSERT_EQUAL_INT(UPDATER_STATE_CHECKING, state_of(&up));

    TEST_ASSERT_EQUAL_INT(FW_OK, updater_step(&up, TEST_INTERVAL_MS));
    TEST_ASSERT_EQUAL_INT(UPDATER_STATE_IDLE, state_of(&up));

    /* Immediately stepping again must not re-enter CHECKING. */
    TEST_ASSERT_EQUAL_INT(FW_OK, updater_step(&up, TEST_INTERVAL_MS + 1U));
    TEST_ASSERT_EQUAL_INT(UPDATER_STATE_IDLE, state_of(&up));
}

/* A zero interval is a real setting: checking is off, not "every loop". */
void test_updater_never_checks_when_the_interval_is_zero(void) {
    updater_t up      = {0};
    updater_cfg_t cfg = test_cfg();

    cfg.check_interval_ms = 0U;
    TEST_ASSERT_EQUAL_INT(FW_OK, updater_init(&up, &cfg));
    TEST_ASSERT_EQUAL_INT(FW_OK, updater_start(&up, 0U));

    TEST_ASSERT_EQUAL_INT(FW_OK, updater_step(&up, 1U));
    TEST_ASSERT_EQUAL_INT(FW_OK, updater_step(&up, 0xFFFFFFFFU));
    TEST_ASSERT_EQUAL_INT(UPDATER_STATE_IDLE, state_of(&up));
}

/* R-LFC-04: stop and deinit are repeatable and safe on a half-built instance. */
void test_updater_stop_and_deinit_are_repeatable(void) {
    updater_t up            = {0};
    const updater_cfg_t cfg = test_cfg();

    TEST_ASSERT_EQUAL_INT(FW_OK, updater_stop(&up)); /* never initialized */
    TEST_ASSERT_EQUAL_INT(FW_OK, updater_deinit(&up));

    TEST_ASSERT_EQUAL_INT(FW_OK, updater_init(&up, &cfg));
    TEST_ASSERT_EQUAL_INT(FW_OK, updater_start(&up, 0U));
    TEST_ASSERT_EQUAL_INT(FW_OK, updater_stop(&up));
    TEST_ASSERT_EQUAL_INT(FW_OK, updater_stop(&up));
    TEST_ASSERT_EQUAL_INT(FW_ERR_STATE, updater_step(&up, TEST_INTERVAL_MS));
    TEST_ASSERT_EQUAL_INT(FW_OK, updater_deinit(&up));
    TEST_ASSERT_EQUAL_INT(FW_OK, updater_deinit(&up));
}

void test_updater_state_str_names_every_state(void) {
    TEST_ASSERT_EQUAL_STRING("UPDATER_STATE_IDLE", updater_state_str(UPDATER_STATE_IDLE));
    TEST_ASSERT_EQUAL_STRING("UPDATER_STATE_FAILED", updater_state_str(UPDATER_STATE_FAILED));
    TEST_ASSERT_EQUAL_STRING("UPDATER_STATE_UNKNOWN", updater_state_str((updater_state_t)99));
}

/* -------------------------- Private functions -------------------------- */

static updater_cfg_t test_cfg(void) {
    updater_cfg_t cfg     = updater_cfg_default();
    cfg.check_interval_ms = TEST_INTERVAL_MS;
    cfg.retry_backoff_ms  = TEST_BACKOFF_MS;
    return cfg;
}

static updater_state_t state_of(const updater_t *up) {
    updater_state_t state = UPDATER_STATE_FAILED;
    TEST_ASSERT_EQUAL_INT(FW_OK, updater_state_get(up, &state));
    return state;
}

/*** end of file ***/
