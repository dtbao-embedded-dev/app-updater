/**
 * @file    runner.c
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

void test_protocol_crc_matches_the_known_answer_vector(void);
void test_protocol_round4_pads_to_the_four_byte_boundary(void);
void test_protocol_accepts_a_minimum_frame_with_no_payload(void);
void test_protocol_hunts_past_leading_garbage(void);
void test_protocol_resumes_one_byte_after_an_oversized_length(void);
void test_protocol_accepts_the_largest_legal_length(void);
void test_protocol_drops_a_frame_whose_crc_is_wrong(void);
void test_protocol_covers_the_pad_bytes_in_the_crc(void);
void test_protocol_never_resolves_the_all_zero_command(void);
void test_protocol_cmd_lookup_separates_unsupported_from_unknown(void);
void test_protocol_rsp_build_writes_status_first(void);
void test_protocol_rsp_build_refuses_a_buffer_too_small(void);
void test_protocol_status_str_names_every_defined_status(void);

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

    RUN_TEST(test_protocol_crc_matches_the_known_answer_vector);
    RUN_TEST(test_protocol_round4_pads_to_the_four_byte_boundary);
    RUN_TEST(test_protocol_accepts_a_minimum_frame_with_no_payload);
    RUN_TEST(test_protocol_hunts_past_leading_garbage);
    RUN_TEST(test_protocol_resumes_one_byte_after_an_oversized_length);
    RUN_TEST(test_protocol_accepts_the_largest_legal_length);
    RUN_TEST(test_protocol_drops_a_frame_whose_crc_is_wrong);
    RUN_TEST(test_protocol_covers_the_pad_bytes_in_the_crc);
    RUN_TEST(test_protocol_never_resolves_the_all_zero_command);
    RUN_TEST(test_protocol_cmd_lookup_separates_unsupported_from_unknown);
    RUN_TEST(test_protocol_rsp_build_writes_status_first);
    RUN_TEST(test_protocol_rsp_build_refuses_a_buffer_too_small);
    RUN_TEST(test_protocol_status_str_names_every_defined_status);

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
