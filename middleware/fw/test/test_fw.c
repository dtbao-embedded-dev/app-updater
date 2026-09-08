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

#include "esp_rom_crc.h"

#include <stdint.h>
#include <string.h>

/* --------------------------- Private macros ---------------------------- */

/* CRC-32 of the ASCII digits "123456789". The canonical check value of the
 * reflected CRC-32 (poly 0xEDB88320, init and xorout 0xFFFFFFFF) that zlib,
 * Ethernet and the ESP32 ROM routine all compute - and, since this commit,
 * fw_crc32_le() too. Fixed outside every implementation on purpose: a wrong
 * CRC agrees with itself and passes everything else. */
#define KNOWN_ANSWER_INPUT "123456789"
#define KNOWN_ANSWER_CRC   0xCBF43926U

/* ------------------------ Public function prototypes ------------------- */

/* The runner in test/host/runner.c calls these. Declaring them here is what
 * satisfies -Wmissing-prototypes under the house warning set (R-BLD-01) --
 * a test function is an external symbol like any other. */
void test_fw_err_str_names_every_defined_code(void);
void test_fw_err_only_ok_is_non_negative(void);
void test_fw_err_str_rejects_an_unknown_code(void);
void test_fw_crc32_matches_the_known_answer_vector(void);
void test_fw_crc32_chains_across_two_buffers(void);
void test_fw_crc32_agrees_with_an_independent_implementation(void);

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

/* THE test that makes fw_crc32_le() usable at all. Every stored record and
 * every wire frame in this firmware is checked with it, so the one number it
 * must produce is fixed here rather than derived. */
void test_fw_crc32_matches_the_known_answer_vector(void) {
    const uint32_t crc = fw_crc32_le(0U, KNOWN_ANSWER_INPUT, sizeof(KNOWN_ANSWER_INPUT) - 1U);

    TEST_ASSERT_EQUAL_HEX32(KNOWN_ANSWER_CRC, crc);
}

/* The upgrade session folds the CRC chunk by chunk instead of re-reading 13 MB
 * of flash at the end, which is only correct if a chain of calls equals one
 * call over the whole buffer. */
void test_fw_crc32_chains_across_two_buffers(void) {
    const uint32_t part = fw_crc32_le(0U, "1234", 4U);
    const uint32_t crc  = fw_crc32_le(part, "56789", 5U);

    TEST_ASSERT_EQUAL_HEX32(KNOWN_ANSWER_CRC, crc);

    /* And a zero-length call is the identity, so a last chunk of nothing
     * cannot change the answer. */
    TEST_ASSERT_EQUAL_HEX32(crc, fw_crc32_le(crc, NULL, 0U));
}

/* The table-driven implementation is checked against the bitwise one in
 * test/host/stub/esp_rom_crc.h, written straight from the polynomial. Two
 * independent routines agreeing on 256 different lengths is what catches a
 * single wrong table entry, which the one known-answer vector above can miss. */
void test_fw_crc32_agrees_with_an_independent_implementation(void) {
    uint8_t buf[256];
    for (size_t i = 0; i < sizeof(buf); ++i) {
        buf[i] = (uint8_t)((i * 7U) + 13U);
    }

    for (uint32_t len = 0U; len <= (uint32_t)sizeof(buf); ++len) {
        TEST_ASSERT_EQUAL_HEX32(esp_rom_crc32_le(0U, buf, len), fw_crc32_le(0U, buf, len));
    }
}

/*** end of file ***/
