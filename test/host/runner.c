/**
 * @file    runner.c
 * @author  dtbao
 * @date    2026-09-06
 * @brief   Unity entry point listing every host test in the repo.
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

/* ------------------------------ Includes ------------------------------- */

#include "unity.h"

/* --------------------- Private function prototypes --------------------- */

/* Declared here rather than in a header: a test file has no public contract,
 * and this list is the one place that says which tests actually run. Adding a
 * test function without adding it here is the way a test silently stops
 * running, so keep the two in step. */
void test_fw_err_str_names_every_defined_code(void);
void test_fw_err_only_ok_is_non_negative(void);
void test_fw_err_str_rejects_an_unknown_code(void);

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

/* Unity calls these around every test. Nothing here owns global state
 * (R-TST-05), so both are empty on purpose. */
void setUp(void)
{
}

void tearDown(void)
{
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_fw_err_str_names_every_defined_code);
    RUN_TEST(test_fw_err_only_ok_is_non_negative);
    RUN_TEST(test_fw_err_str_rejects_an_unknown_code);

    RUN_TEST(test_updater_step_before_start_returns_err_state);
    RUN_TEST(test_updater_init_twice_returns_err_state);
    RUN_TEST(test_updater_rejects_null_arguments);
    RUN_TEST(test_updater_init_rejects_an_interval_past_the_horizon);
    RUN_TEST(test_updater_stays_idle_until_the_interval_elapses);
    RUN_TEST(test_updater_fires_across_the_millisecond_wrap);
    RUN_TEST(test_updater_rearms_after_a_check_finds_nothing);
    RUN_TEST(test_updater_never_checks_when_the_interval_is_zero);
    RUN_TEST(test_updater_stop_and_deinit_are_repeatable);
    RUN_TEST(test_updater_state_str_names_every_state);

    return UNITY_END();
}

/*** end of file ***/
