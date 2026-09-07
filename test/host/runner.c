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

void test_command_serves_exactly_the_documented_opcode_set(void);
void test_command_answers_unsupported_for_hardware_this_board_lacks(void);
void test_command_answers_bad_cmd_for_an_opcode_the_spec_never_defined(void);
void test_command_answers_bad_cmd_for_the_retired_product_id_item(void);
void test_command_checks_length_before_it_checks_value(void);
void test_command_ping_echoes_the_payload_byte_for_byte(void);
void test_command_ping_refuses_a_payload_whose_reply_would_not_fit(void);
void test_command_restart_app_replies_before_it_resets(void);
void test_command_version_reports_the_updater_then_the_firmware(void);
void test_command_version_zeroes_a_slot_that_holds_no_image(void);
void test_command_get_boot_slot_reports_the_running_slot(void);
void test_command_set_boot_slot_arms_the_slot_it_names(void);
void test_command_set_boot_slot_refuses_a_slot_with_no_valid_image(void);
void test_command_reads_both_burned_in_macs(void);
void test_command_maps_a_mac_read_failure_to_hw(void);
void test_command_rejects_null_arguments(void);
void test_command_upgrade_refuses_a_chunk_size_outside_the_band(void);
void test_command_upgrade_accepts_both_ends_of_the_chunk_band(void);
void test_command_upgrade_refuses_an_image_that_does_not_fit_before_erasing(void);
void test_command_upgrade_refuses_the_running_slot(void);
void test_command_upgrade_refuses_an_unknown_target(void);
void test_command_upgrade_refuses_a_write_with_no_session(void);
void test_command_upgrade_refuses_an_offset_out_of_order(void);
void test_command_upgrade_enforces_the_chunk_rules(void);
void test_command_upgrade_transfers_a_whole_image(void);
void test_command_upgrade_end_refuses_a_wrong_image_crc(void);
void test_command_upgrade_end_refuses_an_incomplete_transfer(void);
void test_command_upgrade_begin_again_frees_the_first_session(void);
void test_command_upgrade_refuses_while_the_update_cycle_is_writing(void);
void test_command_upgrade_maps_a_flash_failure_to_hw(void);

void test_cfg_defaults_are_what_an_erased_device_runs_on(void);
void test_cfg_init_falls_back_to_defaults_when_nothing_was_stored(void);
void test_cfg_init_falls_back_to_defaults_when_the_record_is_corrupt(void);
void test_cfg_init_returns_an_adapter_failure_unchanged(void);
void test_cfg_a_saved_value_survives_a_reload(void);
void test_cfg_check_interval_refuses_the_scheduling_horizon(void);
void test_cfg_check_interval_accepts_both_ends_of_its_range(void);
void test_cfg_manifest_url_refuses_a_string_that_does_not_fit(void);
void test_cfg_manifest_url_get_refuses_a_buffer_too_small(void);
void test_cfg_every_accessor_is_err_state_before_init(void);
void test_cfg_rejects_null_arguments(void);
void test_cfg_deinit_is_repeatable_and_safe_half_built(void);
void test_cfg_a_setter_never_writes_to_the_store(void);

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

    RUN_TEST(test_command_serves_exactly_the_documented_opcode_set);
    RUN_TEST(test_command_answers_unsupported_for_hardware_this_board_lacks);
    RUN_TEST(test_command_answers_bad_cmd_for_an_opcode_the_spec_never_defined);
    RUN_TEST(test_command_answers_bad_cmd_for_the_retired_product_id_item);
    RUN_TEST(test_command_checks_length_before_it_checks_value);
    RUN_TEST(test_command_ping_echoes_the_payload_byte_for_byte);
    RUN_TEST(test_command_ping_refuses_a_payload_whose_reply_would_not_fit);
    RUN_TEST(test_command_restart_app_replies_before_it_resets);
    RUN_TEST(test_command_version_reports_the_updater_then_the_firmware);
    RUN_TEST(test_command_version_zeroes_a_slot_that_holds_no_image);
    RUN_TEST(test_command_get_boot_slot_reports_the_running_slot);
    RUN_TEST(test_command_set_boot_slot_arms_the_slot_it_names);
    RUN_TEST(test_command_set_boot_slot_refuses_a_slot_with_no_valid_image);
    RUN_TEST(test_command_reads_both_burned_in_macs);
    RUN_TEST(test_command_maps_a_mac_read_failure_to_hw);
    RUN_TEST(test_command_rejects_null_arguments);
    RUN_TEST(test_command_upgrade_refuses_a_chunk_size_outside_the_band);
    RUN_TEST(test_command_upgrade_accepts_both_ends_of_the_chunk_band);
    RUN_TEST(test_command_upgrade_refuses_an_image_that_does_not_fit_before_erasing);
    RUN_TEST(test_command_upgrade_refuses_the_running_slot);
    RUN_TEST(test_command_upgrade_refuses_an_unknown_target);
    RUN_TEST(test_command_upgrade_refuses_a_write_with_no_session);
    RUN_TEST(test_command_upgrade_refuses_an_offset_out_of_order);
    RUN_TEST(test_command_upgrade_enforces_the_chunk_rules);
    RUN_TEST(test_command_upgrade_transfers_a_whole_image);
    RUN_TEST(test_command_upgrade_end_refuses_a_wrong_image_crc);
    RUN_TEST(test_command_upgrade_end_refuses_an_incomplete_transfer);
    RUN_TEST(test_command_upgrade_begin_again_frees_the_first_session);
    RUN_TEST(test_command_upgrade_refuses_while_the_update_cycle_is_writing);
    RUN_TEST(test_command_upgrade_maps_a_flash_failure_to_hw);

    RUN_TEST(test_cfg_defaults_are_what_an_erased_device_runs_on);
    RUN_TEST(test_cfg_init_falls_back_to_defaults_when_nothing_was_stored);
    RUN_TEST(test_cfg_init_falls_back_to_defaults_when_the_record_is_corrupt);
    RUN_TEST(test_cfg_init_returns_an_adapter_failure_unchanged);
    RUN_TEST(test_cfg_a_saved_value_survives_a_reload);
    RUN_TEST(test_cfg_check_interval_refuses_the_scheduling_horizon);
    RUN_TEST(test_cfg_check_interval_accepts_both_ends_of_its_range);
    RUN_TEST(test_cfg_manifest_url_refuses_a_string_that_does_not_fit);
    RUN_TEST(test_cfg_manifest_url_get_refuses_a_buffer_too_small);
    RUN_TEST(test_cfg_every_accessor_is_err_state_before_init);
    RUN_TEST(test_cfg_rejects_null_arguments);
    RUN_TEST(test_cfg_deinit_is_repeatable_and_safe_half_built);
    RUN_TEST(test_cfg_a_setter_never_writes_to_the_store);

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
