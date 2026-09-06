/**
 * @file    test_command.c
 * @date    2026-09-06
 * @brief   Host tests for the USB command dispatcher and its handlers.
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

/* ------------------------------ Includes ------------------------------- */

#include "unity.h"

#include "command.h"
#include "protocol.h"

#include "esp_fake.h"
#include "esp_partition.h"
#include "esp_rom_crc.h"

#include <string.h>

/* ----------------------------- Static data ----------------------------- */

/* File scope, not stack: command_t carries a PROTOCOL_MAX_FRAME buffer, and a
 * captured reply is another. Every test calls setup() first, so nothing
 * carries between tests (R-TST-05). */
static command_t s_cmd;
static uint8_t s_reply[PROTOCOL_MAX_FRAME];
static uint32_t s_reply_len;
static uint32_t s_reply_count;
static uint32_t s_restarts_when_replied;
static bool s_busy;
static uint8_t s_payload[PROTOCOL_MAX_DATA];

/* --------------------- Private function prototypes --------------------- */

static uint32_t get_le32(const uint8_t *in);
static fw_err_t capture_reply(void *ctx, const uint8_t *data, size_t len);
static bool report_busy(void *ctx);
static void setup(void);
static fw_err_t send(uint32_t command, const uint8_t *data, uint32_t len);
static int32_t reply_status(void);
static uint32_t reply_payload_len(void);
static const uint8_t *reply_payload(void);

/* ------------------------ Public function prototypes ------------------- */

/* The runner in test/host/runner.c calls these. Declaring them here is what
 * satisfies -Wmissing-prototypes under the house warning set (R-BLD-01). */
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

/* -------------------------- Public functions --------------------------- */

/* R2: the served set is a decision, not an accident. This is the list, and
 * anything drifting into or out of it fails here rather than on a bench. */
void test_command_serves_exactly_the_documented_opcode_set(void) {
    static const uint32_t served[] = {
        PROTOCOL_CMD_RESTART_APP, PROTOCOL_CMD_PING,          PROTOCOL_CMD_SET_BOOT_SLOT,
        PROTOCOL_CMD_GET_VERSION, PROTOCOL_CMD_GET_BOOT_SLOT, PROTOCOL_CMD_GET_WIFI_MAC,
        PROTOCOL_CMD_GET_BLE_MAC, PROTOCOL_CMD_UPG_BEGIN,     PROTOCOL_CMD_UPG_WRITE,
        PROTOCOL_CMD_UPG_END,
    };
    uint32_t served_in_map = 0U;

    for (size_t i = 0; i < (sizeof(served) / sizeof(served[0])); ++i) {
        const protocol_cmd_info_t *info = protocol_cmd_lookup(served[i]);
        TEST_ASSERT_NOT_NULL(info);
        TEST_ASSERT_EQUAL_INT(PROTOCOL_CMD_SERVED, info->kind);
    }

    /* And nothing else in the whole 16-bit opcode space is served. */
    for (uint32_t opcode = 0U; opcode <= 0xFFFFU; ++opcode) {
        const protocol_cmd_info_t *info = protocol_cmd_lookup(opcode);
        if ((info != NULL) && (info->kind == PROTOCOL_CMD_SERVED)) {
            served_in_map += 1U;
        }
    }
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(sizeof(served) / sizeof(served[0])), served_in_map);
}

/* -7 is the honest answer for an opcode aimed at hardware this board does not
 * have: the command is real, the peripheral is not. A stub returning OK would
 * ship a unit carrying a check that never checked anything. */
void test_command_answers_unsupported_for_hardware_this_board_lacks(void) {
    setup();

    TEST_ASSERT_EQUAL_INT(FW_OK, send(0x0005U, NULL, 0U)); /* NET_STATUS  */
    TEST_ASSERT_EQUAL_INT32(PROTOCOL_ERR_UNSUPPORTED, reply_status());

    TEST_ASSERT_EQUAL_INT(FW_OK, send(0x0510U, s_payload, 1U)); /* CHK_LCD */
    TEST_ASSERT_EQUAL_INT32(PROTOCOL_ERR_UNSUPPORTED, reply_status());

    TEST_ASSERT_EQUAL_INT(FW_OK, send(0x0301U, s_payload, 1U)); /* MSC_ENABLE */
    TEST_ASSERT_EQUAL_INT32(PROTOCOL_ERR_UNSUPPORTED, reply_status());

    /* And the COMMAND is echoed back unchanged, so a host can match the
     * answer to the question it asked. */
    TEST_ASSERT_EQUAL_HEX32(0x0301U, get_le32(&s_reply[4]));
}

void test_command_answers_bad_cmd_for_an_opcode_the_spec_never_defined(void) {
    setup();

    TEST_ASSERT_EQUAL_INT(FW_OK, send(0x0207U, NULL, 0U));
    TEST_ASSERT_EQUAL_INT32(PROTOCOL_ERR_BAD_CMD, reply_status());

    TEST_ASSERT_EQUAL_INT(FW_OK, send(0x0700U, NULL, 0U));
    TEST_ASSERT_EQUAL_INT32(PROTOCOL_ERR_BAD_CMD, reply_status());

    /* The all-zero word, which item numbering starts at 0x01 to keep free. */
    TEST_ASSERT_EQUAL_INT(FW_OK, send(0x0000U, NULL, 0U));
    TEST_ASSERT_EQUAL_INT32(PROTOCOL_ERR_BAD_CMD, reply_status());
}

/* Get System 0x06 was PRODUCT_ID and is retired. A tool built against the old
 * map has to be told the command is gone - which is -2 - rather than reaching
 * whatever number took its place. */
void test_command_answers_bad_cmd_for_the_retired_product_id_item(void) {
    setup();

    TEST_ASSERT_EQUAL_INT(FW_OK, send(0x0206U, NULL, 0U));
    TEST_ASSERT_EQUAL_INT32(PROTOCOL_ERR_BAD_CMD, reply_status());
}

/* R5: the two checks are separate so a tool knows which half to fix. A
 * one-byte payload carrying 2 is a value problem; a two-byte payload is a
 * length problem, whatever it contains. */
void test_command_checks_length_before_it_checks_value(void) {
    static const uint8_t two_bytes[] = {PROTOCOL_SLOT_FIRMWARE, 0x00U};
    static const uint8_t bad_value[] = {2U};
    static const uint8_t good[]      = {PROTOCOL_SLOT_FIRMWARE};

    setup();

    TEST_ASSERT_EQUAL_INT(FW_OK, send(PROTOCOL_CMD_SET_BOOT_SLOT, NULL, 0U));
    TEST_ASSERT_EQUAL_INT32(PROTOCOL_ERR_BAD_LEN, reply_status());

    TEST_ASSERT_EQUAL_INT(FW_OK, send(PROTOCOL_CMD_SET_BOOT_SLOT, two_bytes, 2U));
    TEST_ASSERT_EQUAL_INT32(PROTOCOL_ERR_BAD_LEN, reply_status());

    TEST_ASSERT_EQUAL_INT(FW_OK, send(PROTOCOL_CMD_SET_BOOT_SLOT, bad_value, 1U));
    TEST_ASSERT_EQUAL_INT32(PROTOCOL_ERR_BAD_ARG, reply_status());

    /* Nothing was armed by either refusal. */
    TEST_ASSERT_EQUAL_INT(0, (int)esp_fake_boot_slot_armed());

    TEST_ASSERT_EQUAL_INT(FW_OK, send(PROTOCOL_CMD_SET_BOOT_SLOT, good, 1U));
    TEST_ASSERT_EQUAL_INT32(PROTOCOL_OK, reply_status());
}

void test_command_ping_echoes_the_payload_byte_for_byte(void) {
    static const uint8_t abc[] = {0x41U, 0x42U, 0x43U};

    setup();

    /* n = 0 is a bare liveness check: status only, no payload. */
    TEST_ASSERT_EQUAL_INT(FW_OK, send(PROTOCOL_CMD_PING, NULL, 0U));
    TEST_ASSERT_EQUAL_INT32(PROTOCOL_OK, reply_status());
    TEST_ASSERT_EQUAL_UINT32(0U, reply_payload_len());

    TEST_ASSERT_EQUAL_INT(FW_OK, send(PROTOCOL_CMD_PING, abc, 3U));
    TEST_ASSERT_EQUAL_INT32(PROTOCOL_OK, reply_status());
    TEST_ASSERT_EQUAL_UINT32(3U, reply_payload_len());
    TEST_ASSERT_EQUAL_HEX8_ARRAY(abc, reply_payload(), 3U);

    /* The largest echo that can fit a reply frame, exercised end to end. */
    for (uint32_t i = 0; i < (PROTOCOL_MAX_DATA - PROTOCOL_STATUS_LEN); ++i) {
        s_payload[i] = (uint8_t)(i & 0xFFU);
    }
    TEST_ASSERT_EQUAL_INT(
        FW_OK, send(PROTOCOL_CMD_PING, s_payload, PROTOCOL_MAX_DATA - PROTOCOL_STATUS_LEN));
    TEST_ASSERT_EQUAL_INT32(PROTOCOL_OK, reply_status());
    TEST_ASSERT_EQUAL_UINT32(PROTOCOL_MAX_DATA - PROTOCOL_STATUS_LEN, reply_payload_len());
    TEST_ASSERT_EQUAL_HEX8_ARRAY(s_payload, reply_payload(),
                                 PROTOCOL_MAX_DATA - PROTOCOL_STATUS_LEN);
}

/* A gap in the source spec, pinned here so it cannot be rediscovered on a
 * bench. The spec says PING echoes 0 .. PROTOCOL_MAX_DATA bytes AND that
 * RSP.LENGTH is 4 + REQ.LENGTH - which for a request at the cap asks for a
 * reply LENGTH of 32776, past the same cap. The real ceiling is therefore
 * PROTOCOL_MAX_DATA - PROTOCOL_STATUS_LEN, and anything above it is a length
 * this command cannot serve: -3, not a truncated echo and not silence. */
void test_command_ping_refuses_a_payload_whose_reply_would_not_fit(void) {
    setup();

    TEST_ASSERT_EQUAL_INT(
        FW_OK, send(PROTOCOL_CMD_PING, s_payload, PROTOCOL_MAX_DATA - PROTOCOL_STATUS_LEN + 1U));
    TEST_ASSERT_EQUAL_INT32(PROTOCOL_ERR_BAD_LEN, reply_status());

    TEST_ASSERT_EQUAL_INT(FW_OK, send(PROTOCOL_CMD_PING, s_payload, PROTOCOL_MAX_DATA));
    TEST_ASSERT_EQUAL_INT32(PROTOCOL_ERR_BAD_LEN, reply_status());
}

/* The ordering is the whole contract: a host that never sees the OK cannot
 * tell "restarting" from "cable fell out". The capture records how many resets
 * had happened at the moment the reply was written - it has to be zero. */
void test_command_restart_app_replies_before_it_resets(void) {
    setup();

    TEST_ASSERT_EQUAL_INT(FW_OK, send(PROTOCOL_CMD_RESTART_APP, NULL, 0U));

    TEST_ASSERT_EQUAL_INT32(PROTOCOL_OK, reply_status());
    TEST_ASSERT_EQUAL_UINT32(1U, s_reply_count);
    TEST_ASSERT_EQUAL_UINT32(0U, s_restarts_when_replied);
    TEST_ASSERT_EQUAL_UINT32(1U, esp_fake_restart_count());

    /* And the reply was given time to leave before the reset. */
    TEST_ASSERT_TRUE(esp_fake_delay_total_ms() >= COMMAND_RESTART_GRACE_MS);
}

/* [0:16] is the image running now - on this product the updater, which the
 * source spec calls BL2 - and [16:32] is the app_firmware slot. */
void test_command_version_reports_the_updater_then_the_firmware(void) {
    setup();
    esp_fake_set_self_version("0.1.0");
    esp_fake_set_slot_version(ESP_PARTITION_SUBTYPE_APP_OTA_1, "2.4.0");

    TEST_ASSERT_EQUAL_INT(FW_OK, send(PROTOCOL_CMD_GET_VERSION, NULL, 0U));
    TEST_ASSERT_EQUAL_INT32(PROTOCOL_OK, reply_status());
    TEST_ASSERT_EQUAL_UINT32(PROTOCOL_VERSION_LEN, reply_payload_len());
    TEST_ASSERT_EQUAL_STRING("0.1.0", (const char *)&reply_payload()[0]);
    TEST_ASSERT_EQUAL_STRING("2.4.0", (const char *)&reply_payload()[PROTOCOL_VERSION_FIELD_LEN]);
}

/* "Never written" is an answer, not a failure: a blank slot reads back as
 * sixteen zero bytes with PROTOCOL_OK, so a host tells unset from a read that
 * went wrong without a second command. */
void test_command_version_zeroes_a_slot_that_holds_no_image(void) {
    static const uint8_t zeros[PROTOCOL_VERSION_FIELD_LEN] = {0};

    setup();
    esp_fake_set_slot_version(ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL);

    TEST_ASSERT_EQUAL_INT(FW_OK, send(PROTOCOL_CMD_GET_VERSION, NULL, 0U));
    TEST_ASSERT_EQUAL_INT32(PROTOCOL_OK, reply_status());
    TEST_ASSERT_EQUAL_UINT32(PROTOCOL_VERSION_LEN, reply_payload_len());
    TEST_ASSERT_EQUAL_HEX8_ARRAY(zeros, &reply_payload()[PROTOCOL_VERSION_FIELD_LEN],
                                 PROTOCOL_VERSION_FIELD_LEN);
}

void test_command_get_boot_slot_reports_the_running_slot(void) {
    setup();

    esp_fake_set_running_slot(ESP_PARTITION_SUBTYPE_APP_OTA_0);
    TEST_ASSERT_EQUAL_INT(FW_OK, send(PROTOCOL_CMD_GET_BOOT_SLOT, NULL, 0U));
    TEST_ASSERT_EQUAL_INT32(PROTOCOL_OK, reply_status());
    TEST_ASSERT_EQUAL_UINT32(1U, reply_payload_len());
    TEST_ASSERT_EQUAL_HEX8(PROTOCOL_SLOT_UPDATER, reply_payload()[0]);

    esp_fake_set_running_slot(ESP_PARTITION_SUBTYPE_APP_OTA_1);
    TEST_ASSERT_EQUAL_INT(FW_OK, send(PROTOCOL_CMD_GET_BOOT_SLOT, NULL, 0U));
    TEST_ASSERT_EQUAL_HEX8(PROTOCOL_SLOT_FIRMWARE, reply_payload()[0]);
}

/* Set arms the NEXT boot and Get reports the one running now, so the two are
 * deliberately not symmetric: this test states that by arming ota_1 while
 * ota_0 keeps running. */
void test_command_set_boot_slot_arms_the_slot_it_names(void) {
    static const uint8_t to_firmware[] = {PROTOCOL_SLOT_FIRMWARE};
    static const uint8_t to_updater[]  = {PROTOCOL_SLOT_UPDATER};

    setup();

    TEST_ASSERT_EQUAL_INT(FW_OK, send(PROTOCOL_CMD_SET_BOOT_SLOT, to_firmware, 1U));
    TEST_ASSERT_EQUAL_INT32(PROTOCOL_OK, reply_status());
    TEST_ASSERT_EQUAL_INT(ESP_PARTITION_SUBTYPE_APP_OTA_1, (int)esp_fake_boot_slot_armed());

    TEST_ASSERT_EQUAL_INT(FW_OK, send(PROTOCOL_CMD_SET_BOOT_SLOT, to_updater, 1U));
    TEST_ASSERT_EQUAL_INT(ESP_PARTITION_SUBTYPE_APP_OTA_0, (int)esp_fake_boot_slot_armed());

    /* A Get right after a Set still reads the slot running now, not the armed
     * one - correct, and the reason a tool must say "will boot X after
     * restart" rather than read it back. */
    TEST_ASSERT_EQUAL_INT(FW_OK, send(PROTOCOL_CMD_GET_BOOT_SLOT, NULL, 0U));
    TEST_ASSERT_EQUAL_HEX8(PROTOCOL_SLOT_UPDATER, reply_payload()[0]);
}

/* Arming a slot whose image does not validate is a state a jig can fix by
 * transferring one - so -5, which the spec defines as retryable, not -6. */
void test_command_set_boot_slot_refuses_a_slot_with_no_valid_image(void) {
    static const uint8_t to_firmware[] = {PROTOCOL_SLOT_FIRMWARE};

    setup();
    esp_fake_fail_set_boot(ESP_ERR_OTA_VALIDATE_FAILED);

    TEST_ASSERT_EQUAL_INT(FW_OK, send(PROTOCOL_CMD_SET_BOOT_SLOT, to_firmware, 1U));
    TEST_ASSERT_EQUAL_INT32(PROTOCOL_ERR_STATE, reply_status());
    TEST_ASSERT_EQUAL_INT(0, (int)esp_fake_boot_slot_armed());
}

/* Both MACs come from eFuse, so this product answers them with no radio and no
 * BLE stack - which is why they are served here and the ATE checks are not. */
void test_command_reads_both_burned_in_macs(void) {
    static const uint8_t wifi[] = {0x11U, 0x22U, 0x33U, 0x44U, 0x55U, 0x66U};
    static const uint8_t bt[]   = {0x11U, 0x22U, 0x33U, 0x44U, 0x55U, 0x67U};

    setup();
    esp_fake_set_mac(ESP_MAC_WIFI_STA, wifi);
    esp_fake_set_mac(ESP_MAC_BT, bt);

    TEST_ASSERT_EQUAL_INT(FW_OK, send(PROTOCOL_CMD_GET_WIFI_MAC, NULL, 0U));
    TEST_ASSERT_EQUAL_INT32(PROTOCOL_OK, reply_status());
    TEST_ASSERT_EQUAL_UINT32(PROTOCOL_MAC_LEN, reply_payload_len());
    TEST_ASSERT_EQUAL_HEX8_ARRAY(wifi, reply_payload(), PROTOCOL_MAC_LEN);

    TEST_ASSERT_EQUAL_INT(FW_OK, send(PROTOCOL_CMD_GET_BLE_MAC, NULL, 0U));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(bt, reply_payload(), PROTOCOL_MAC_LEN);
}

void test_command_maps_a_mac_read_failure_to_hw(void) {
    setup();
    esp_fake_fail_read_mac(ESP_FAIL);

    TEST_ASSERT_EQUAL_INT(FW_OK, send(PROTOCOL_CMD_GET_WIFI_MAC, NULL, 0U));
    TEST_ASSERT_EQUAL_INT32(PROTOCOL_ERR_HW, reply_status());
    TEST_ASSERT_EQUAL_UINT32(0U, reply_payload_len());
}

/* A NULL argument is the one input that gets no response: there is nowhere to
 * send it and nothing to echo. */
void test_command_rejects_null_arguments(void) {
    protocol_req_t req = {.command = PROTOCOL_CMD_PING, .len = 0U, .data = NULL};
    command_cfg_t cfg  = {.on_reply = capture_reply, .reply_ctx = NULL};

    setup();

    TEST_ASSERT_EQUAL_INT(FW_ERR_PARAM, command_on_frame(NULL, &req));
    TEST_ASSERT_EQUAL_INT(FW_ERR_PARAM, command_on_frame(&s_cmd, NULL));
    TEST_ASSERT_EQUAL_UINT32(0U, s_reply_count);

    TEST_ASSERT_EQUAL_INT(FW_ERR_PARAM, command_init(NULL, &cfg));
    TEST_ASSERT_EQUAL_INT(FW_ERR_PARAM, command_init(&s_cmd, NULL));
    TEST_ASSERT_EQUAL_INT(FW_ERR_PARAM, command_deinit(NULL));

    /* on_reply has no sensible default, so a config without one is refused
     * rather than leaving a dispatcher that answers into the void. */
    cfg.on_reply = NULL;
    TEST_ASSERT_EQUAL_INT(FW_ERR_PARAM, command_init(&s_cmd, &cfg));

    /* And init twice is a lifecycle error, not a silent re-init. */
    cfg.on_reply = capture_reply;
    TEST_ASSERT_EQUAL_INT(FW_ERR_STATE, command_init(&s_cmd, &cfg));
}

/* -------------------------- Private functions -------------------------- */

static uint32_t get_le32(const uint8_t *in) {
    return (uint32_t)in[0] | ((uint32_t)in[1] << 8) | ((uint32_t)in[2] << 16) |
           ((uint32_t)in[3] << 24);
}

/* Records the frame and, crucially, how many resets had happened when it
 * arrived - that number is what proves RESTART_APP's ordering. */
static fw_err_t capture_reply(void *ctx, const uint8_t *data, size_t len) {
    (void)ctx;

    TEST_ASSERT_NOT_NULL(data);
    TEST_ASSERT_TRUE(len <= sizeof(s_reply));
    memcpy(s_reply, data, len);
    s_reply_len = (uint32_t)len;
    s_reply_count += 1U;
    s_restarts_when_replied = esp_fake_restart_count();
    return FW_OK;
}

static bool report_busy(void *ctx) {
    (void)ctx;
    return s_busy;
}

static void setup(void) {
    const command_cfg_t cfg = {
        .on_reply  = capture_reply,
        .reply_ctx = NULL,
        .is_busy   = report_busy,
        .busy_ctx  = NULL,
    };

    esp_fake_reset();
    memset(&s_cmd, 0, sizeof(s_cmd));
    memset(s_reply, 0, sizeof(s_reply));
    memset(s_payload, 0, sizeof(s_payload));
    s_reply_len             = 0U;
    s_reply_count           = 0U;
    s_restarts_when_replied = 0U;
    s_busy                  = false;

    TEST_ASSERT_EQUAL_INT(FW_OK, command_init(&s_cmd, &cfg));
}

/* Hands the dispatcher one request the way the parser would, then leaves the
 * response in s_reply for the assertions to read. */
static fw_err_t send(uint32_t command, const uint8_t *data, uint32_t len) {
    const protocol_req_t req = {
        .command = command,
        .len     = len,
        .data    = (len > 0U) ? data : NULL,
    };

    s_reply_len   = 0U;
    s_reply_count = 0U;
    return command_on_frame(&s_cmd, &req);
}

/* Every assertion about a reply goes through these three, so each one also
 * re-checks the frame envelope: response magic, an echoed COMMAND, a LENGTH
 * that covers its own status, and a CRC over exactly what was sent. */
static int32_t reply_status(void) {
    /* Exactly one response per request, checked on every single assertion
     * rather than in one test: a handler that answers twice corrupts the
     * channel for the NEXT command, so the failure would surface far from its
     * cause. */
    TEST_ASSERT_EQUAL_UINT32(1U, s_reply_count);
    TEST_ASSERT_TRUE(s_reply_len >= PROTOCOL_OVERHEAD_LEN + PROTOCOL_STATUS_LEN);
    TEST_ASSERT_EQUAL_HEX32(PROTOCOL_HDR_RSP, get_le32(&s_reply[0]));

    const uint32_t n      = get_le32(&s_reply[8]);
    const uint32_t padded = protocol_round4(n);
    TEST_ASSERT_TRUE(n >= PROTOCOL_STATUS_LEN);
    TEST_ASSERT_EQUAL_UINT32(PROTOCOL_OVERHEAD_LEN + padded, s_reply_len);
    TEST_ASSERT_EQUAL_HEX32(esp_rom_crc32_le(0U, s_reply, PROTOCOL_PREFIX_LEN + padded),
                            get_le32(&s_reply[PROTOCOL_PREFIX_LEN + padded]));

    return (int32_t)get_le32(&s_reply[PROTOCOL_PREFIX_LEN]);
}

static uint32_t reply_payload_len(void) {
    return get_le32(&s_reply[8]) - PROTOCOL_STATUS_LEN;
}

static const uint8_t *reply_payload(void) {
    return &s_reply[PROTOCOL_PREFIX_LEN + PROTOCOL_STATUS_LEN];
}

/*** end of file ***/
