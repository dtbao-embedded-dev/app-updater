/**
 * @file    test_protocol.c
 * @date    2026-09-06
 * @brief   Host tests for the USB command channel frame codec and command map.
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

/* ------------------------------ Includes ------------------------------- */

#include "unity.h"

#include "protocol.h"

#include "esp_rom_crc.h"

#include <string.h>

/* --------------------------- Private macros ---------------------------- */

/* CRC-32 of the ASCII digits "123456789". The canonical check value of the
 * reflected CRC-32 (poly 0xEDB88320, init and xorout 0xFFFFFFFF) that zlib,
 * Ethernet and esp_rom_crc32_le() all compute. */
#define KNOWN_ANSWER_INPUT "123456789"
#define KNOWN_ANSWER_CRC   0xCBF43926U

/* ----------------------------- Static data ----------------------------- */

/* File scope, not stack: a parser is PROTOCOL_MAX_FRAME plus a length, and a
 * test that puts two of those and a frame buffer on the stack asks for ~100 KB
 * of it. Every test resets what it touches before it reads it, so nothing
 * carries between tests (R-TST-05). */
static protocol_parser_t s_parser;
static uint8_t s_frame[PROTOCOL_MAX_FRAME];
static uint8_t s_stream[PROTOCOL_MAX_FRAME * 2U];
static uint8_t s_payload[PROTOCOL_MAX_DATA];

/* --------------------- Private function prototypes --------------------- */

static void put_le32(uint8_t *out, uint32_t value);
static uint32_t get_le32(const uint8_t *in);
static uint32_t build_req(uint8_t *out, uint32_t command, const uint8_t *data, uint32_t n);
static uint32_t feed_count(const uint8_t *bytes, uint32_t n, protocol_req_t *out_last);

/* ------------------------ Public function prototypes ------------------- */

/* The runner in test/host/runner.c calls these. Declaring them here is what
 * satisfies -Wmissing-prototypes under the house warning set (R-BLD-01). */
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

/* -------------------------- Public functions --------------------------- */

/* THE test that makes every other test in this file mean something. The frames
 * below are built with the host fake in test/host/stub/esp_rom_crc.h and
 * checked by the parser, which on target uses the ROM routine. A wrong CRC
 * would agree with itself and pass all of them; only a value fixed outside
 * both implementations catches that. */
void test_protocol_crc_matches_the_known_answer_vector(void) {
    const uint32_t crc = esp_rom_crc32_le(0U, (const uint8_t *)KNOWN_ANSWER_INPUT,
                                          (uint32_t)strlen(KNOWN_ANSWER_INPUT));

    TEST_ASSERT_EQUAL_HEX32(KNOWN_ANSWER_CRC, crc);
}

/* DATA is padded so every field stays 4-byte aligned and the CRC32 lands on a
 * boundary. Minimum frame is therefore 16 bytes. */
void test_protocol_round4_pads_to_the_four_byte_boundary(void) {
    TEST_ASSERT_EQUAL_UINT32(0U, protocol_round4(0U));
    TEST_ASSERT_EQUAL_UINT32(4U, protocol_round4(1U));
    TEST_ASSERT_EQUAL_UINT32(4U, protocol_round4(3U));
    TEST_ASSERT_EQUAL_UINT32(4U, protocol_round4(4U));
    TEST_ASSERT_EQUAL_UINT32(8U, protocol_round4(5U));
    TEST_ASSERT_EQUAL_UINT32(PROTOCOL_MAX_DATA, protocol_round4(PROTOCOL_MAX_DATA));
}

void test_protocol_accepts_a_minimum_frame_with_no_payload(void) {
    protocol_req_t req;
    const uint32_t n = build_req(s_frame, PROTOCOL_CMD_PING, NULL, 0U);

    TEST_ASSERT_EQUAL_UINT32(PROTOCOL_OVERHEAD_LEN, n);
    TEST_ASSERT_EQUAL_UINT32(1U, feed_count(s_frame, n, &req));
    TEST_ASSERT_EQUAL_HEX32(PROTOCOL_CMD_PING, req.command);
    TEST_ASSERT_EQUAL_UINT32(0U, req.len);
}

/* Step 1 of the parser rules: slide one byte at a time over anything that is
 * not a header. The garbage here ends in a half-written magic, which is the
 * case a naive "compare four bytes then give up" parser loses the next frame
 * to. */
void test_protocol_hunts_past_leading_garbage(void) {
    static const uint8_t junk[] = {0x00U, 0xFFU, 0x41U, 0x52U, 0x51U, 0x11U, 0x52U};
    protocol_req_t req;
    const uint32_t frame_len = build_req(s_frame, PROTOCOL_CMD_GET_VERSION, NULL, 0U);

    memcpy(s_stream, junk, sizeof(junk));
    memcpy(&s_stream[sizeof(junk)], s_frame, frame_len);

    TEST_ASSERT_EQUAL_UINT32(1U, feed_count(s_stream, (uint32_t)sizeof(junk) + frame_len, &req));
    TEST_ASSERT_EQUAL_HEX32(PROTOCOL_CMD_GET_VERSION, req.command);
}

/* Step 2, and the reason it is worded "from the byte after the failed header"
 * and not "after the frame that header described".
 *
 * The stream opens with a good magic and four filler bytes, and then a REAL
 * frame begins at offset 8 - which means the bogus header's LENGTH field IS
 * that frame's magic, read as a little-endian word: 0x3E3E5152, comfortably
 * past PROTOCOL_MAX_DATA. So the reject fires with the good frame already
 * sitting inside the twelve bytes being thrown away.
 *
 * Only a parser that resumes at offset 1 and re-examines what it consumed
 * finds it. Resuming at offset 12 steps over the magic, and trusting the bad
 * LENGTH enough to skip round4(n) + 4 bytes swallows far more than that. Both
 * answer nothing, and one corrupt length would then cost the channel every
 * frame behind it - which is why the placement here is deliberate rather than
 * convenient. */
void test_protocol_resumes_one_byte_after_an_oversized_length(void) {
    protocol_req_t req;
    const uint32_t frame_len = build_req(s_frame, PROTOCOL_CMD_UPG_END, NULL, 0U);

    put_le32(&s_stream[0], PROTOCOL_HDR_REQ);
    put_le32(&s_stream[4], 0xAAAAAAAAU); /* filler in the COMMAND slot */
    memcpy(&s_stream[8], s_frame, frame_len);

    /* State the trap rather than trusting it: the word the bogus header reads
     * as LENGTH is the magic of the frame hidden inside it. */
    TEST_ASSERT_EQUAL_HEX32(PROTOCOL_HDR_REQ, get_le32(&s_stream[8]));
    TEST_ASSERT_TRUE(get_le32(&s_stream[8]) > PROTOCOL_MAX_DATA);

    TEST_ASSERT_EQUAL_UINT32(1U, feed_count(s_stream, 8U + frame_len, &req));
    TEST_ASSERT_EQUAL_HEX32(PROTOCOL_CMD_UPG_END, req.command);
}

/* The cap is a legal value, not a rejected one: PROTOCOL_MAX_DATA is sized to
 * carry a 32 KB chunk plus its 4-byte offset, so an off-by-one here would
 * refuse the largest UPG_WRITE the band allows. */
void test_protocol_accepts_the_largest_legal_length(void) {
    protocol_req_t req;
    uint32_t frame_len;

    for (uint32_t i = 0; i < PROTOCOL_MAX_DATA; ++i) {
        s_payload[i] = (uint8_t)(i & 0xFFU);
    }
    frame_len = build_req(s_frame, PROTOCOL_CMD_UPG_WRITE, s_payload, PROTOCOL_MAX_DATA);

    TEST_ASSERT_EQUAL_UINT32(PROTOCOL_MAX_FRAME, frame_len);
    TEST_ASSERT_EQUAL_UINT32(1U, feed_count(s_frame, frame_len, &req));
    TEST_ASSERT_EQUAL_UINT32(PROTOCOL_MAX_DATA, req.len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(s_payload, req.data, PROTOCOL_MAX_DATA);
}

/* Step 4: a frame whose CRC does not check out is dropped with NO response -
 * its COMMAND cannot be trusted enough to echo. The good frame fed afterwards
 * is what proves the drop did not take the channel with it. */
void test_protocol_drops_a_frame_whose_crc_is_wrong(void) {
    protocol_req_t req;
    static const uint8_t body[] = {0x01U, 0x02U, 0x03U, 0x04U};
    const uint32_t bad_len      = build_req(s_frame, PROTOCOL_CMD_SET_BOOT_SLOT, body, 1U);
    uint32_t good_len;

    s_frame[PROTOCOL_PREFIX_LEN] ^= 0xFFU; /* corrupt DATA, leave the CRC */
    memcpy(s_stream, s_frame, bad_len);

    good_len = build_req(s_frame, PROTOCOL_CMD_PING, body, 4U);
    memcpy(&s_stream[bad_len], s_frame, good_len);

    TEST_ASSERT_EQUAL_UINT32(1U, feed_count(s_stream, bad_len + good_len, &req));
    TEST_ASSERT_EQUAL_HEX32(PROTOCOL_CMD_PING, req.command);
}

/* The pad bytes are zero but the CRC still covers them, and only the first `n`
 * bytes reach the handler. Both halves are asserted here because dropping
 * either one is silent: excluding the pad from the CRC still parses every
 * frame a well-behaved host sends, and handing the pad to the handler still
 * looks right until a payload length stops being a multiple of 4. */
void test_protocol_covers_the_pad_bytes_in_the_crc(void) {
    static const uint8_t abc[] = {0x41U, 0x42U, 0x43U}; /* "ABC" */
    protocol_req_t req;
    const uint32_t frame_len = build_req(s_frame, PROTOCOL_CMD_PING, abc, 3U);

    TEST_ASSERT_EQUAL_UINT32(PROTOCOL_OVERHEAD_LEN + 4U, frame_len);
    TEST_ASSERT_EQUAL_UINT32(0U, s_frame[PROTOCOL_PREFIX_LEN + 3U]); /* the pad */

    TEST_ASSERT_EQUAL_UINT32(1U, feed_count(s_frame, frame_len, &req));
    TEST_ASSERT_EQUAL_UINT32(3U, req.len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(abc, req.data, 3U);

    /* Touch nothing but the pad byte: if the CRC did not cover it, this frame
     * would still be accepted. */
    s_frame[PROTOCOL_PREFIX_LEN + 3U] = 0xFFU;
    TEST_ASSERT_EQUAL_UINT32(0U, feed_count(s_frame, frame_len, &req));
}

/* Item numbering starts at 0x01 in every range, so the all-zero word is never
 * a command. It is what a zeroed buffer or a half-built struct produces, and
 * the one value worth spending an item number to keep meaningless. */
void test_protocol_never_resolves_the_all_zero_command(void) {
    TEST_ASSERT_NULL(protocol_cmd_lookup(0x00000000U));
    TEST_ASSERT_NULL(protocol_cmd_lookup(0x0000U));
    TEST_ASSERT_NULL(protocol_cmd_lookup(0x0100U));
    TEST_ASSERT_NULL(protocol_cmd_lookup(0x0600U));
}

/* -2 versus -7 is the difference a host acts on, so the map has to hold every
 * opcode the spec defines - not just the ones this build serves. An opcode the
 * spec retired (Get System 0x06, once PRODUCT_ID) must read as absent, so an
 * old tool learns the command is gone instead of reaching whatever took the
 * number. */
void test_protocol_cmd_lookup_separates_unsupported_from_unknown(void) {
    const protocol_cmd_info_t *served    = protocol_cmd_lookup(PROTOCOL_CMD_PING);
    const protocol_cmd_info_t *defined   = protocol_cmd_lookup(0x0005U); /* NET_STATUS */
    const protocol_cmd_info_t *ate_check = protocol_cmd_lookup(0x0510U); /* CHK_LCD */

    TEST_ASSERT_NOT_NULL(served);
    TEST_ASSERT_EQUAL_INT(PROTOCOL_CMD_SERVED, served->kind);
    TEST_ASSERT_EQUAL_UINT32(PROTOCOL_LEN_ANY, served->req_len);

    TEST_ASSERT_NOT_NULL(defined);
    TEST_ASSERT_EQUAL_INT(PROTOCOL_CMD_UNSUPPORTED, defined->kind);

    TEST_ASSERT_NOT_NULL(ate_check);
    TEST_ASSERT_EQUAL_INT(PROTOCOL_CMD_UNSUPPORTED, ate_check->kind);
    TEST_ASSERT_EQUAL_UINT32(1U, ate_check->req_len);

    TEST_ASSERT_NULL(protocol_cmd_lookup(0x0206U)); /* retired PRODUCT_ID */
    TEST_ASSERT_NULL(protocol_cmd_lookup(0x0207U)); /* never assigned */
    TEST_ASSERT_NULL(protocol_cmd_lookup(0x0700U)); /* no such range */
}

void test_protocol_rsp_build_writes_status_first(void) {
    static const uint8_t slot[] = {PROTOCOL_SLOT_FIRMWARE};
    uint32_t n = protocol_rsp_build(s_frame, sizeof(s_frame), PROTOCOL_CMD_GET_VERSION,
                                    PROTOCOL_ERR_BAD_CMD, NULL, 0U);

    TEST_ASSERT_EQUAL_UINT32(PROTOCOL_OVERHEAD_LEN + 4U, n);
    TEST_ASSERT_EQUAL_HEX32(PROTOCOL_HDR_RSP, get_le32(&s_frame[0]));
    TEST_ASSERT_EQUAL_HEX32(PROTOCOL_CMD_GET_VERSION, get_le32(&s_frame[4]));
    TEST_ASSERT_EQUAL_UINT32(PROTOCOL_STATUS_LEN, get_le32(&s_frame[8]));
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFEU, get_le32(&s_frame[PROTOCOL_PREFIX_LEN]));
    TEST_ASSERT_EQUAL_HEX32(esp_rom_crc32_le(0U, s_frame, PROTOCOL_PREFIX_LEN + 4U),
                            get_le32(&s_frame[PROTOCOL_PREFIX_LEN + 4U]));

    /* A payload follows the status, and LENGTH counts both. One byte of slot
     * therefore reports n = 5 and pads DATA to 8. */
    n = protocol_rsp_build(s_frame, sizeof(s_frame), PROTOCOL_CMD_GET_BOOT_SLOT, PROTOCOL_OK, slot,
                           1U);
    TEST_ASSERT_EQUAL_UINT32(PROTOCOL_OVERHEAD_LEN + 8U, n);
    TEST_ASSERT_EQUAL_UINT32(PROTOCOL_STATUS_LEN + 1U, get_le32(&s_frame[8]));
    TEST_ASSERT_EQUAL_HEX32(0U, get_le32(&s_frame[PROTOCOL_PREFIX_LEN]));
    TEST_ASSERT_EQUAL_HEX8(PROTOCOL_SLOT_FIRMWARE, s_frame[PROTOCOL_PREFIX_LEN + 4U]);
}

/* Returning 0 and writing nothing is what lets a caller send nothing, instead
 * of putting a truncated frame on the wire for the host to resync past. */
void test_protocol_rsp_build_refuses_a_buffer_too_small(void) {
    memset(s_frame, 0xA5U, PROTOCOL_OVERHEAD_LEN);

    TEST_ASSERT_EQUAL_UINT32(0U, protocol_rsp_build(s_frame, PROTOCOL_OVERHEAD_LEN,
                                                    PROTOCOL_CMD_PING, PROTOCOL_OK, NULL, 0U));
    TEST_ASSERT_EQUAL_HEX8(0xA5U, s_frame[0]);
    TEST_ASSERT_EQUAL_UINT32(
        0U, protocol_rsp_build(NULL, sizeof(s_frame), PROTOCOL_CMD_PING, PROTOCOL_OK, NULL, 0U));
    TEST_ASSERT_EQUAL_UINT32(0U, protocol_rsp_build(s_frame, sizeof(s_frame), PROTOCOL_CMD_PING,
                                                    PROTOCOL_OK, NULL,
                                                    PROTOCOL_MAX_DATA - PROTOCOL_STATUS_LEN + 1U));
}

void test_protocol_status_str_names_every_defined_status(void) {
    static const protocol_status_t all[] = {
        PROTOCOL_OK,          PROTOCOL_ERR_CRC,   PROTOCOL_ERR_BAD_CMD, PROTOCOL_ERR_BAD_LEN,
        PROTOCOL_ERR_BAD_ARG, PROTOCOL_ERR_STATE, PROTOCOL_ERR_HW,      PROTOCOL_ERR_UNSUPPORTED,
    };

    for (size_t i = 0; i < (sizeof(all) / sizeof(all[0])); ++i) {
        TEST_ASSERT_NOT_EQUAL_INT(0, strcmp("PROTOCOL_ERR_UNKNOWN", protocol_status_str(all[i])));
    }
    TEST_ASSERT_EQUAL_STRING("PROTOCOL_ERR_UNKNOWN", protocol_status_str((protocol_status_t)-99));
}

/* -------------------------- Private functions -------------------------- */

static void put_le32(uint8_t *out, uint32_t value) {
    out[0] = (uint8_t)(value & 0xFFU);
    out[1] = (uint8_t)((value >> 8) & 0xFFU);
    out[2] = (uint8_t)((value >> 16) & 0xFFU);
    out[3] = (uint8_t)((value >> 24) & 0xFFU);
}

static uint32_t get_le32(const uint8_t *in) {
    return (uint32_t)in[0] | ((uint32_t)in[1] << 8) | ((uint32_t)in[2] << 16) |
           ((uint32_t)in[3] << 24);
}

/* Assembles one request the way a host would, so a test states the bytes it
 * puts on the wire rather than borrowing the parser's own idea of them. */
static uint32_t build_req(uint8_t *out, uint32_t command, const uint8_t *data, uint32_t n) {
    const uint32_t padded = protocol_round4(n);

    put_le32(&out[0], PROTOCOL_HDR_REQ);
    put_le32(&out[4], command);
    put_le32(&out[8], n);
    memset(&out[PROTOCOL_PREFIX_LEN], 0, padded);
    if (n > 0U) {
        memcpy(&out[PROTOCOL_PREFIX_LEN], data, n);
    }
    put_le32(&out[PROTOCOL_PREFIX_LEN + padded],
             esp_rom_crc32_le(0U, out, PROTOCOL_PREFIX_LEN + padded));

    return PROTOCOL_OVERHEAD_LEN + padded;
}

/* Feeds a byte stream through a fresh parser and reports how many requests
 * completed, keeping the last one. Counting rather than returning a bool is
 * what catches a parser that answers a corrupt frame as well as a good one. */
static uint32_t feed_count(const uint8_t *bytes, uint32_t n, protocol_req_t *out_last) {
    uint32_t completed = 0U;

    protocol_parser_reset(&s_parser);
    for (uint32_t i = 0; i < n; ++i) {
        protocol_req_t req;
        if (protocol_parser_feed(&s_parser, bytes[i], &req)) {
            ++completed;
            *out_last = req;
        }
    }

    return completed;
}

/*** end of file ***/
