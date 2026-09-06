/**
 * @file    test_fw.c
 * @date    2026-09-06
 * @brief   Host tests for the project-wide status code.
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

/* ------------------------------ Includes ------------------------------- */

#include "unity.h"

#include "fw.h"

#include <string.h>

/* ------------------------ Public function prototypes ------------------- */

/* The runner in test/host/runner.c calls these. Declaring them here is what
 * satisfies -Wmissing-prototypes under the house warning set (R-BLD-01) --
 * a test function is an external symbol like any other. */
void test_fw_err_str_names_every_defined_code(void);
void test_fw_err_only_ok_is_non_negative(void);
void test_fw_err_str_rejects_an_unknown_code(void);

/* -------------------------- Public functions --------------------------- */

/* R-ERR-06: every defined code has a name. A code added to the enum without a
 * case in fw_err_str() reaches this test as "FW_ERR_UNKNOWN". */
void test_fw_err_str_names_every_defined_code(void) {
    static const fw_err_t codes[] = {
        FW_OK,           FW_ERR_PARAM,       FW_ERR_STATE, FW_ERR_TIMEOUT,
        FW_ERR_NO_SPACE, FW_ERR_NOT_FOUND,   FW_ERR_IO,    FW_ERR_CRC,
        FW_ERR_NO_MEM,   FW_ERR_UNSUPPORTED,
    };

    for (size_t i = 0; i < (sizeof(codes) / sizeof(codes[0])); ++i) {
        TEST_ASSERT_NOT_EQUAL_INT(0, strcmp("FW_ERR_UNKNOWN", fw_err_str(codes[i])));
    }
}

/* R-ERR-02: zero is success, every failure is negative. */
void test_fw_err_only_ok_is_non_negative(void) {
    TEST_ASSERT_EQUAL_INT(0, (int)FW_OK);
    TEST_ASSERT_LESS_THAN_INT(0, (int)FW_ERR_PARAM);
    TEST_ASSERT_LESS_THAN_INT(0, (int)FW_ERR_NO_MEM);
}

/* An out-of-range value cast in from outside must not fall off the switch. */
void test_fw_err_str_rejects_an_unknown_code(void) {
    TEST_ASSERT_EQUAL_STRING("FW_ERR_UNKNOWN", fw_err_str((fw_err_t)-999));
}

/*** end of file ***/
