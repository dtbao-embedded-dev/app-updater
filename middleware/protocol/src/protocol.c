/**
 * @file    protocol.c
 * @date    2026-09-06
 * @brief   Wire format and command map of the USB command channel: frame
 *          codec, status codes, and the opcode table this build serves.
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

/* ------------------------------ Includes ------------------------------- */

#include "protocol.h"

#include <string.h>

/* --------------------------- Private macros ---------------------------- */

/** Bytes of magic the hunt has to see before it can compare anything. */
#define MAGIC_LEN 4U

/* ---------------------------- Private types ---------------------------- */

/* ----------------------------- Static data ----------------------------- */

/* Every opcode the source spec defines, whether this build serves it or not.
 *
 * The unserved rows are what make PROTOCOL_ERR_UNSUPPORTED possible: without
 * them a host asking for CHK_LCD would be told PROTOCOL_ERR_BAD_CMD, which
 * says "no such command, fix your request" when the truth is "right command,
 * wrong hardware". A gap in the numbering is deliberate and stays a gap - a
 * retired item (Get System 0x06, once PRODUCT_ID) is absent here on purpose,
 * so a tool built against the old map learns the command is gone instead of
 * reaching whatever took the number.
 *
 * `req_len` is the exact REQ LENGTH, or PROTOCOL_LEN_ANY where only the
 * handler can judge it. It is checked before any value is looked at, so a tool
 * is told which half of its request to fix.
 *
 * Sorted by opcode for reading, not for searching: 46 rows scanned linearly
 * once per frame is nothing next to a USB transfer.
 * ponytail: linear scan, binary search if the map ever reaches hundreds of rows. */
static const protocol_cmd_info_t s_cmd_map[] = {
    /* --- Action 0x00: imperative operations, nothing stored behind them --- */
    {PROTOCOL_CMD_RESTART_APP, 0U, PROTOCOL_CMD_SERVED},
    {0x0002U, 0U, PROTOCOL_CMD_UNSUPPORTED},  /* FACTORY_RESET  */
    {0x0003U, 98U, PROTOCOL_CMD_UNSUPPORTED}, /* WIFI_CONNECT   */
    {0x0004U, 0U, PROTOCOL_CMD_UNSUPPORTED},  /* WIFI_SCAN      */
    {0x0005U, 0U, PROTOCOL_CMD_UNSUPPORTED},  /* NET_STATUS     */
    {PROTOCOL_CMD_PING, PROTOCOL_LEN_ANY, PROTOCOL_CMD_SERVED},

    /* --- Set System 0x01: write device state --- */
    {PROTOCOL_CMD_SET_BOOT_SLOT, 1U, PROTOCOL_CMD_SERVED},
    {0x0102U, 16U, PROTOCOL_CMD_UNSUPPORTED}, /* GUID           */

    /* --- Get System 0x02: read device state --- */
    {PROTOCOL_CMD_GET_VERSION, 0U, PROTOCOL_CMD_SERVED},
    {PROTOCOL_CMD_GET_BOOT_SLOT, 0U, PROTOCOL_CMD_SERVED},
    {PROTOCOL_CMD_GET_WIFI_MAC, 0U, PROTOCOL_CMD_SERVED},
    {PROTOCOL_CMD_GET_BLE_MAC, 0U, PROTOCOL_CMD_SERVED},
    {0x0205U, 0U, PROTOCOL_CMD_UNSUPPORTED}, /* GUID           */

    /* --- Set Config 0x03: write a config-registry row.
     * 0x0305 WIFI_STA, 0x0309 LOG_TAGS and 0x03FF CFG_VER are absent because a
     * set on a read-only row is not a command at all, which is -2, not -7. --- */
    {0x0301U, 1U, PROTOCOL_CMD_UNSUPPORTED},               /* MSC_ENABLE     */
    {0x0303U, 1U, PROTOCOL_CMD_UNSUPPORTED},               /* LANGUAGE       */
    {0x0304U, PROTOCOL_LEN_ANY, PROTOCOL_CMD_UNSUPPORTED}, /* BLE_NAME       */
    {0x0306U, PROTOCOL_LEN_ANY, PROTOCOL_CMD_UNSUPPORTED}, /* CUSTOMER_ID    */
    {0x0307U, 1U, PROTOCOL_CMD_UNSUPPORTED},               /* LCD_BRIGHTNESS */
    {0x0308U, PROTOCOL_LEN_ANY, PROTOCOL_CMD_UNSUPPORTED}, /* LOG_LEVEL      */
    {0x030AU, 1U, PROTOCOL_CMD_UNSUPPORTED},               /* NET_MODE       */

    /* --- Get Config 0x04: read the same rows back --- */
    {0x0401U, 0U, PROTOCOL_CMD_UNSUPPORTED},               /* MSC_ENABLE     */
    {0x0403U, 0U, PROTOCOL_CMD_UNSUPPORTED},               /* LANGUAGE       */
    {0x0404U, 0U, PROTOCOL_CMD_UNSUPPORTED},               /* BLE_NAME       */
    {0x0405U, 0U, PROTOCOL_CMD_UNSUPPORTED},               /* WIFI_STA       */
    {0x0406U, 0U, PROTOCOL_CMD_UNSUPPORTED},               /* CUSTOMER_ID    */
    {0x0407U, 0U, PROTOCOL_CMD_UNSUPPORTED},               /* LCD_BRIGHTNESS */
    {0x0408U, PROTOCOL_LEN_ANY, PROTOCOL_CMD_UNSUPPORTED}, /* LOG_LEVEL      */
    {0x0409U, 0U, PROTOCOL_CMD_UNSUPPORTED},               /* LOG_TAGS       */
    {0x040AU, 0U, PROTOCOL_CMD_UNSUPPORTED},               /* NET_MODE       */
    {0x04FFU, 0U, PROTOCOL_CMD_UNSUPPORTED},               /* CFG_VER        */

    /* --- ATE 0x05: 01-0F provisioning, 10-1F hardware check. None of it can
     * run here: no identity store, and no LCD, touch panel, LED, BLE stack,
     * Wi-Fi driver, Ethernet PHY, SD card or RTC on this board. --- */
    {0x0501U, 4U, PROTOCOL_CMD_UNSUPPORTED}, /* SET_MFG_DATE   */
    {0x0502U, 0U, PROTOCOL_CMD_UNSUPPORTED}, /* GET_MFG_DATE   */
    {0x0503U, 1U, PROTOCOL_CMD_UNSUPPORTED}, /* PROVISION_LOCK */
    {0x0510U, 1U, PROTOCOL_CMD_UNSUPPORTED}, /* CHK_LCD        */
    {0x0511U, 4U, PROTOCOL_CMD_UNSUPPORTED}, /* CHK_TOUCH      */
    {0x0512U, 1U, PROTOCOL_CMD_UNSUPPORTED}, /* CHK_LED        */
    {0x0513U, 0U, PROTOCOL_CMD_UNSUPPORTED}, /* CHK_BLE        */
    {0x0514U, 0U, PROTOCOL_CMD_UNSUPPORTED}, /* CHK_WIFI       */
    {0x0515U, 0U, PROTOCOL_CMD_UNSUPPORTED}, /* CHK_ETH        */
    {0x0516U, 0U, PROTOCOL_CMD_UNSUPPORTED}, /* CHK_SDCARD     */
    {0x0517U, 0U, PROTOCOL_CMD_UNSUPPORTED}, /* CHK_RTC        */
    {0x0518U, 1U, PROTOCOL_CMD_UNSUPPORTED}, /* CHK_BACKLIGHT  */
    {0x0519U, 8U, PROTOCOL_CMD_UNSUPPORTED}, /* CHK_TOUCH_AT   */

    /* --- Upgrade 0x06: the reason this channel exists --- */
    {PROTOCOL_CMD_UPG_BEGIN, PROTOCOL_UPG_BEGIN_LEN, PROTOCOL_CMD_SERVED},
    {PROTOCOL_CMD_UPG_WRITE, PROTOCOL_LEN_ANY, PROTOCOL_CMD_SERVED},
    {PROTOCOL_CMD_UPG_END, 0U, PROTOCOL_CMD_SERVED},

    /* --- Core dump 0x07: read the panic record back off a running unit ---
     * A range of its own rather than three numbers in Get System, because
     * 0x0206 is retired and must stay absent. Note what is NOT here: no BEGIN
     * and no END. A read has no session to open - every DUMP_READ carries its
     * own offset and length, so a host may retry any chunk in any order and a
     * transfer that dies half way costs nothing. Upgrade needs a session
     * because it mutates a slot; this only looks. */
    {PROTOCOL_CMD_DUMP_INFO, 0U, PROTOCOL_CMD_SERVED},
    {PROTOCOL_CMD_DUMP_READ, PROTOCOL_DUMP_READ_LEN, PROTOCOL_CMD_SERVED},
    {PROTOCOL_CMD_DUMP_ERASE, 0U, PROTOCOL_CMD_SERVED},
};

/* --------------------- Private function prototypes --------------------- */

static void put_le32(uint8_t *out, uint32_t value);
static uint32_t get_le32(const uint8_t *in);
static void resync(protocol_parser_t *parser, uint32_t from);
static bool take_frame(protocol_parser_t *parser, protocol_req_t *out_req);

/* -------------------------- Public functions --------------------------- */

const char *protocol_status_str(protocol_status_t status) {
    /* No `default` label: -Wswitch-enum (R-BLD-01) then fails the build when a
     * status is added here without a name. The fallthrough after the switch
     * covers a value cast in from outside. */
    switch (status) {
        case PROTOCOL_OK:
            return "PROTOCOL_OK";
        case PROTOCOL_ERR_CRC:
            return "PROTOCOL_ERR_CRC";
        case PROTOCOL_ERR_BAD_CMD:
            return "PROTOCOL_ERR_BAD_CMD";
        case PROTOCOL_ERR_BAD_LEN:
            return "PROTOCOL_ERR_BAD_LEN";
        case PROTOCOL_ERR_BAD_ARG:
            return "PROTOCOL_ERR_BAD_ARG";
        case PROTOCOL_ERR_STATE:
            return "PROTOCOL_ERR_STATE";
        case PROTOCOL_ERR_HW:
            return "PROTOCOL_ERR_HW";
        case PROTOCOL_ERR_UNSUPPORTED:
            return "PROTOCOL_ERR_UNSUPPORTED";
    }

    return "PROTOCOL_ERR_UNKNOWN";
}

const protocol_cmd_info_t *protocol_cmd_lookup(uint32_t command) {
    for (size_t i = 0; i < (sizeof(s_cmd_map) / sizeof(s_cmd_map[0])); ++i) {
        if (s_cmd_map[i].command == command) {
            return &s_cmd_map[i];
        }
    }

    /* Not in the spec at all - including the all-zero word, which is why item
     * numbering starts at 0x01 in every range. */
    return NULL;
}

void protocol_parser_reset(protocol_parser_t *parser) {
    if (parser == NULL) {
        return;
    }
    parser->len  = 0U;
    parser->done = 0U;
}

bool protocol_parser_feed(protocol_parser_t *parser, uint8_t byte, protocol_req_t *out_req) {
    if ((parser == NULL) || (out_req == NULL)) {
        return false;
    }

    /* The request handed out last time pointed into `buf`, so the frame was
     * left in place. It has been read by now; drop it and carry on with
     * whatever followed it. */
    if (parser->done > 0U) {
        resync(parser, parser->done);
        parser->done = 0U;
    }

    /* Unreachable by construction: this function only returns without taking a
     * frame while `len` is short of a committed total, and every committed
     * total is at most PROTOCOL_MAX_FRAME. Guarded anyway, because a public
     * boundary must not be one refactor away from a buffer overrun
     * (R-SRC-06). */
    if (parser->len >= PROTOCOL_MAX_FRAME) {
        parser->len = 0U;
    }

    parser->buf[parser->len] = byte;
    parser->len += 1U;

    return take_frame(parser, out_req);
}

uint32_t protocol_rsp_build(uint8_t *out, uint32_t out_cap, uint32_t command,
                            protocol_status_t status, const uint8_t *payload,
                            uint32_t payload_len) {
    if (out == NULL) {
        return 0U;
    }
    if (payload_len > (PROTOCOL_MAX_DATA - PROTOCOL_STATUS_LEN)) {
        return 0U;
    }
    if ((payload == NULL) && (payload_len > 0U)) {
        return 0U;
    }

    const uint32_t n      = PROTOCOL_STATUS_LEN + payload_len;
    const uint32_t padded = protocol_round4(n);
    const uint32_t total  = PROTOCOL_OVERHEAD_LEN + padded;

    /* Nothing is written on a refusal, so a caller may test the return value
     * and send nothing - better than putting a truncated frame on the wire for
     * the host to resync past. */
    if (out_cap < total) {
        return 0U;
    }

    put_le32(&out[0], PROTOCOL_HDR_RSP);
    put_le32(&out[4], command);
    put_le32(&out[8], n);

    /* Zero first, so the pad bytes the CRC covers are the zeros the spec says
     * they are, whatever the caller left in the buffer. */
    memset(&out[PROTOCOL_PREFIX_LEN], 0, padded);

    /* A negative status converts to its two's complement word, which is what
     * the wire carries: -2 goes out as FE FF FF FF. */
    put_le32(&out[PROTOCOL_PREFIX_LEN], (uint32_t)status);
    if (payload_len > 0U) {
        memcpy(&out[PROTOCOL_PREFIX_LEN + PROTOCOL_STATUS_LEN], payload, payload_len);
    }

    put_le32(&out[PROTOCOL_PREFIX_LEN + padded],
             fw_crc32_le(0U, out, PROTOCOL_PREFIX_LEN + padded));

    return total;
}

/* -------------------------- Private functions -------------------------- */

/* Arguments are checked at the public boundary (R-SRC-06); helpers assume it. */

static void put_le32(uint8_t *out, uint32_t value) {
    out[0] = (uint8_t)(value & 0xFFU);
    out[1] = (uint8_t)((value >> 8) & 0xFFU);
    out[2] = (uint8_t)((value >> 16) & 0xFFU);
    out[3] = (uint8_t)((value >> 24) & 0xFFU);
}

/* Byte-wise, not a cast to uint32_t*: a frame arrives at whatever alignment
 * the buffer happens to give it, and -Wcast-align (R-BLD-01) is right to
 * refuse the cast. */
static uint32_t get_le32(const uint8_t *in) {
    return (uint32_t)in[0] | ((uint32_t)in[1] << 8) | ((uint32_t)in[2] << 16) |
           ((uint32_t)in[3] << 24);
}

/**
 * Drops the first `from` bytes, then slides forward to the next request magic.
 *
 * One forward pass and one move, rather than a shift per byte: a 32 KB frame
 * that fails its CRC would otherwise cost a 32 KB memmove for every byte of
 * whatever follows it, which is a denial of service a host can trigger with
 * garbage.
 *
 * With no magic in what is left, the last three bytes are kept - a magic may
 * straddle the boundary with bytes that have not arrived yet.
 */
static void resync(protocol_parser_t *parser, uint32_t from) {
    if (from >= parser->len) {
        parser->len = 0U;
        return;
    }

    uint32_t at = parser->len; /* no magic found yet */
    for (uint32_t i = from; (i + MAGIC_LEN) <= parser->len; ++i) {
        if (get_le32(&parser->buf[i]) == PROTOCOL_HDR_REQ) {
            at = i;
            break;
        }
    }

    if (at == parser->len) {
        const uint32_t keep = MAGIC_LEN - 1U;
        at                  = (parser->len > keep) ? (parser->len - keep) : 0U;
        if (at < from) {
            at = from;
        }
    }

    parser->len -= at;
    if (parser->len > 0U) {
        (void)memmove(parser->buf, &parser->buf[at], parser->len);
    }
}

/**
 * Runs the parser rules over what the buffer holds, taking at most one frame.
 *
 * Steps 1 to 5 of the spec, in order, and every failure resumes the hunt from
 * the byte after the failed header rather than from behind the frame the
 * header claimed - a corrupt LENGTH must not be trusted far enough to swallow
 * the frames behind it.
 */
static bool take_frame(protocol_parser_t *parser, protocol_req_t *out_req) {
    for (;;) {
        if (parser->len < MAGIC_LEN) {
            return false;
        }
        if (get_le32(parser->buf) != PROTOCOL_HDR_REQ) {
            resync(parser, 1U);
            continue;
        }
        if (parser->len < PROTOCOL_PREFIX_LEN) {
            return false;
        }

        const uint32_t n = get_le32(&parser->buf[8]);
        if (n > PROTOCOL_MAX_DATA) {
            resync(parser, 1U);
            continue;
        }

        const uint32_t padded = protocol_round4(n);
        const uint32_t total  = PROTOCOL_OVERHEAD_LEN + padded;
        if (parser->len < total) {
            return false;
        }

        /* The CRC covers HEADER, COMMAND, LENGTH and DATA including the zero
         * padding - so header corruption is caught too, and a pad byte cannot
         * be tampered with unnoticed. */
        const uint32_t covered = PROTOCOL_PREFIX_LEN + padded;
        if (get_le32(&parser->buf[covered]) != fw_crc32_le(0U, parser->buf, covered)) {
            /* Dropped silently, with no response: the COMMAND cannot be
             * trusted enough to echo. */
            resync(parser, 1U);
            continue;
        }

        out_req->command = get_le32(&parser->buf[4]);
        out_req->len     = n;
        out_req->data    = (n > 0U) ? &parser->buf[PROTOCOL_PREFIX_LEN] : NULL;

        /* Left in place so `data` stays readable; the next feed drops it. */
        parser->done = total;
        return true;
    }
}

/*** end of file ***/
