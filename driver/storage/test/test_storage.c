/**
 * @file    test_storage.c
 * @date    2026-09-07
 * @brief   Host tests for the opaque blob store: its lifecycle, its size
 *          verdicts, and the read-compare-write wear guard.
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

/* ------------------------------ Includes ------------------------------- */

#include "unity.h"

#include "storage.h"

#include "nvs.h"
#include "nvs_fake.h"

#include <string.h>

/* ----------------------------- Static data ----------------------------- */

static storage_t s_st;

/* --------------------- Private function prototypes --------------------- */

static void setup(void);

/* ------------------------ Public function prototypes ------------------- */

/* The runner in test/host/runner.c calls these. Declaring them here is what
 * satisfies -Wmissing-prototypes under the house warning set (R-BLD-01). */
void test_storage_err_str_names_every_defined_code(void);
void test_storage_load_reports_not_found_before_anything_is_written(void);
void test_storage_saves_and_reads_the_same_bytes_back(void);
void test_storage_save_of_identical_bytes_writes_nothing(void);
void test_storage_save_of_changed_bytes_writes_once(void);
void test_storage_reports_a_stored_blob_that_does_not_fit_as_damaged(void);
void test_storage_refuses_a_blob_past_its_ceiling(void);
void test_storage_erases_an_unusable_partition_once_and_carries_on(void);
void test_storage_every_operation_is_err_state_before_init(void);
void test_storage_rejects_null_arguments(void);
void test_storage_deinit_is_repeatable_and_safe_half_built(void);

/* -------------------------- Public functions --------------------------- */

/* R-ERR-06: every defined code has a name. A code added to the enum without a
 * case in storage_err_str() reaches this test as "STORAGE_ERR_UNKNOWN". */
void test_storage_err_str_names_every_defined_code(void) {
    static const storage_err_t codes[] = {
        STORAGE_OK,      STORAGE_ERR_PARAM,     STORAGE_ERR_STATE,
        STORAGE_ERR_IO,  STORAGE_ERR_NOT_FOUND, STORAGE_ERR_NO_SPACE,
        STORAGE_ERR_CRC, STORAGE_ERR_NO_MEM,
    };

    for (size_t i = 0; i < (sizeof(codes) / sizeof(codes[0])); ++i) {
        TEST_ASSERT_NOT_EQUAL_INT(0, strcmp("STORAGE_ERR_UNKNOWN", storage_err_str(codes[i])));
    }
    TEST_ASSERT_EQUAL_STRING("STORAGE_ERR_UNKNOWN", storage_err_str((storage_err_t)-999));
}

/* A blank unit is the ordinary first boot, not a failure - and it has to be
 * distinguishable from a damaged record, because one takes the defaults
 * quietly and the other deserves a log line. */
void test_storage_load_reports_not_found_before_anything_is_written(void) {
    uint8_t out[16] = {0};
    size_t len      = 0U;

    setup();

    TEST_ASSERT_EQUAL_INT(STORAGE_ERR_NOT_FOUND, storage_blob_load(&s_st, out, sizeof(out), &len));
    TEST_ASSERT_EQUAL_UINT32(0U, (uint32_t)len);
}

void test_storage_saves_and_reads_the_same_bytes_back(void) {
    static const uint8_t written[] = {0xDEU, 0xADU, 0xBEU, 0xEFU, 0x00U, 0x7FU};
    uint8_t out[sizeof(written)]   = {0};
    size_t len                     = 0U;

    setup();

    TEST_ASSERT_EQUAL_INT(STORAGE_OK, storage_blob_save(&s_st, written, sizeof(written)));
    TEST_ASSERT_EQUAL_INT(STORAGE_OK, storage_blob_load(&s_st, out, sizeof(out), &len));

    TEST_ASSERT_EQUAL_UINT32((uint32_t)sizeof(written), (uint32_t)len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(written, out, sizeof(written));

    /* An embedded zero is data, not a terminator: the store is told a length. */
    TEST_ASSERT_EQUAL_HEX8(0x00U, out[4]);
}

/* R-CFG-05, and the reason storage_blob_save() reads before it writes: NVS
 * wear is measured in sector erases, so a caller that saves the settings every
 * cycle must not cost an erase every cycle. */
void test_storage_save_of_identical_bytes_writes_nothing(void) {
    static const uint8_t record[] = {1U, 2U, 3U, 4U};

    setup();

    TEST_ASSERT_EQUAL_INT(STORAGE_OK, storage_blob_save(&s_st, record, sizeof(record)));
    TEST_ASSERT_EQUAL_UINT32(1U, nvs_fake_write_count());

    /* Ten more saves of the same bytes, and not one of them reaches flash. */
    for (unsigned i = 0; i < 10U; ++i) {
        TEST_ASSERT_EQUAL_INT(STORAGE_OK, storage_blob_save(&s_st, record, sizeof(record)));
    }
    TEST_ASSERT_EQUAL_UINT32(1U, nvs_fake_write_count());
    TEST_ASSERT_EQUAL_UINT32(1U, nvs_fake_commit_count());
}

/* The other half of the same guard: skipping an identical write must not turn
 * into skipping a real one. */
void test_storage_save_of_changed_bytes_writes_once(void) {
    static const uint8_t first[]  = {1U, 2U, 3U, 4U};
    static const uint8_t second[] = {1U, 2U, 3U, 5U};
    static const uint8_t longer[] = {1U, 2U, 3U, 4U, 5U};

    setup();

    TEST_ASSERT_EQUAL_INT(STORAGE_OK, storage_blob_save(&s_st, first, sizeof(first)));
    TEST_ASSERT_EQUAL_INT(STORAGE_OK, storage_blob_save(&s_st, second, sizeof(second)));
    TEST_ASSERT_EQUAL_UINT32(2U, nvs_fake_write_count());

    /* Same prefix, different length, is a different blob. */
    TEST_ASSERT_EQUAL_INT(STORAGE_OK, storage_blob_save(&s_st, longer, sizeof(longer)));
    TEST_ASSERT_EQUAL_UINT32(3U, nvs_fake_write_count());
    TEST_ASSERT_EQUAL_UINT32((uint32_t)sizeof(longer), (uint32_t)nvs_fake_stored_len());
}

/* A blob of a length this build does not use is not one this build wrote. It
 * comes back as damaged rather than absent, because the caller's response to
 * damaged is to log it and take the defaults - and NVS copies none of it, so
 * there is nothing else to report. */
void test_storage_reports_a_stored_blob_that_does_not_fit_as_damaged(void) {
    static const uint8_t big[64] = {0xA5U};
    uint8_t out[16]              = {0};
    size_t len                   = 0U;

    setup();
    nvs_fake_preload(big, sizeof(big));

    TEST_ASSERT_EQUAL_INT(STORAGE_ERR_CRC, storage_blob_load(&s_st, out, sizeof(out), &len));
    TEST_ASSERT_EQUAL_UINT32(0U, (uint32_t)len);
}

void test_storage_refuses_a_blob_past_its_ceiling(void) {
    static uint8_t huge[STORAGE_BLOB_MAX + 1U];
    uint8_t out[STORAGE_BLOB_MAX + 1U] = {0};
    size_t len                         = 0U;

    setup();

    TEST_ASSERT_EQUAL_INT(STORAGE_ERR_NO_SPACE, storage_blob_save(&s_st, huge, sizeof(huge)));
    TEST_ASSERT_EQUAL_UINT32(0U, nvs_fake_write_count());

    /* And the ceiling is checked before the state, on the way in and out. */
    TEST_ASSERT_EQUAL_INT(STORAGE_ERR_NO_SPACE, storage_blob_load(&s_st, out, sizeof(out), &len));
}

/* The driver owns the subsystem init, so it owns this recovery too: a
 * partition left unusable by a truncated or newer-format image is erased once
 * and init retried, because a unit that cannot read its settings must still
 * boot and take the defaults. */
void test_storage_erases_an_unusable_partition_once_and_carries_on(void) {
    const storage_cfg_t cfg = storage_cfg_default();

    nvs_fake_reset();
    memset(&s_st, 0, sizeof(s_st));
    nvs_fake_fail_flash_init(ESP_ERR_NVS_NO_FREE_PAGES);

    TEST_ASSERT_EQUAL_INT(STORAGE_OK, storage_init(&s_st, &cfg));
    TEST_ASSERT_EQUAL_UINT32(1U, nvs_fake_erase_count());
    TEST_ASSERT_TRUE(nvs_fake_is_open());

    /* An init that fails for any other reason is not erased away - losing the
     * settings has to be the response to an unusable partition only. */
    TEST_ASSERT_EQUAL_INT(STORAGE_OK, storage_deinit(&s_st));
    nvs_fake_reset();
    memset(&s_st, 0, sizeof(s_st));
    nvs_fake_fail_flash_init(ESP_FAIL);

    TEST_ASSERT_EQUAL_INT(STORAGE_ERR_IO, storage_init(&s_st, &cfg));
    TEST_ASSERT_EQUAL_UINT32(0U, nvs_fake_erase_count());
}

void test_storage_every_operation_is_err_state_before_init(void) {
    uint8_t buf[8] = {0};
    size_t len     = 0U;

    nvs_fake_reset();
    memset(&s_st, 0, sizeof(s_st));

    TEST_ASSERT_EQUAL_INT(STORAGE_ERR_STATE, storage_blob_load(&s_st, buf, sizeof(buf), &len));
    TEST_ASSERT_EQUAL_INT(STORAGE_ERR_STATE, storage_blob_save(&s_st, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_UINT32(0U, nvs_fake_write_count());
}

void test_storage_rejects_null_arguments(void) {
    const storage_cfg_t cfg = storage_cfg_default();
    uint8_t buf[8]          = {0};
    size_t len              = 0U;

    setup();

    TEST_ASSERT_EQUAL_INT(STORAGE_ERR_PARAM, storage_init(NULL, &cfg));
    TEST_ASSERT_EQUAL_INT(STORAGE_ERR_PARAM, storage_init(&s_st, NULL));
    TEST_ASSERT_EQUAL_INT(STORAGE_ERR_PARAM, storage_blob_load(NULL, buf, sizeof(buf), &len));
    TEST_ASSERT_EQUAL_INT(STORAGE_ERR_PARAM, storage_blob_load(&s_st, NULL, sizeof(buf), &len));
    TEST_ASSERT_EQUAL_INT(STORAGE_ERR_PARAM, storage_blob_load(&s_st, buf, sizeof(buf), NULL));
    TEST_ASSERT_EQUAL_INT(STORAGE_ERR_PARAM, storage_blob_load(&s_st, buf, 0U, &len));
    TEST_ASSERT_EQUAL_INT(STORAGE_ERR_PARAM, storage_blob_save(NULL, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_INT(STORAGE_ERR_PARAM, storage_blob_save(&s_st, NULL, sizeof(buf)));
    TEST_ASSERT_EQUAL_INT(STORAGE_ERR_PARAM, storage_blob_save(&s_st, buf, 0U));
    TEST_ASSERT_EQUAL_INT(STORAGE_ERR_PARAM, storage_deinit(NULL));
}

/* R-LFC-04: deinit is repeatable and safe on an instance init never finished,
 * because that is the shape of every bring-up failure path. */
void test_storage_deinit_is_repeatable_and_safe_half_built(void) {
    const storage_cfg_t cfg = storage_cfg_default();

    setup();

    TEST_ASSERT_EQUAL_INT(STORAGE_OK, storage_deinit(&s_st));
    TEST_ASSERT_FALSE(nvs_fake_is_open());
    TEST_ASSERT_EQUAL_INT(STORAGE_OK, storage_deinit(&s_st));

    /* A second init on the zeroed instance is legal; a third, on the open one,
     * is not. */
    TEST_ASSERT_EQUAL_INT(STORAGE_OK, storage_init(&s_st, &cfg));
    TEST_ASSERT_EQUAL_INT(STORAGE_ERR_STATE, storage_init(&s_st, &cfg));
}

/* -------------------------- Private functions -------------------------- */

static void setup(void) {
    const storage_cfg_t cfg = storage_cfg_default();

    nvs_fake_reset();
    memset(&s_st, 0, sizeof(s_st));
    TEST_ASSERT_EQUAL_INT(STORAGE_OK, storage_init(&s_st, &cfg));
}

/*** end of file ***/
