/**
 * @file    command_dump.c
 * @date    2026-09-07
 * @brief   The core dump range, 0x07: report what the last panic left in
 *          flash, hand its bytes back a chunk at a time, and clear it.
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

/* ------------------------------ Includes ------------------------------- */

#include "command_priv.h"

#include "coredump.h"
#include "esp_log.h"

#include <stddef.h>

/* --------------------------- Private macros ---------------------------- */

/* Field offsets in DUMP_READ's fixed 8-byte payload. The map pins the width,
 * so the length check in command_on_frame() has already run and these indexes
 * cannot be out of range (R5). */
#define REQ_OFFSET_AT 0U
#define REQ_LEN_AT    4U

/* Field offsets in DUMP_INFO's 8-byte response. The three reserved bytes exist
 * so `size` lands on a 4-byte boundary a host can read with one unpack. */
#define RSP_STATE_AT 0U
#define RSP_SIZE_AT  4U

/* ----------------------------- Static data ----------------------------- */

static const char *TAG = "cmd_dump";

/* --------------------- Private function prototypes --------------------- */

static uint32_t get_le32(const uint8_t *in);
static void put_le32(uint8_t *out, uint32_t value);

/* -------------------------- Public functions --------------------------- */

protocol_status_t command_dump_info(uint8_t *payload, uint32_t *payload_len) {
    coredump_state_t state = COREDUMP_ABSENT;
    uint32_t size          = 0U;

    const coredump_err_t err = coredump_info_get(&state, &size);
    if (err != COREDUMP_OK) {
        /* Nothing about the request is wrong and no device state a host can
         * reach will fix it, so -6 rather than -5. */
        ESP_LOGE(TAG, "dump info: %s", coredump_err_str(err));
        return PROTOCOL_ERR_HW;
    }

    /* An absent dump is reported, not refused: a host that got -5 here could
     * not tell a healthy unit from a command it had got wrong. Same doctrine
     * as an unset version in VERSION. */
    payload[RSP_STATE_AT] = (uint8_t)state;
    put_le32(&payload[RSP_SIZE_AT], size);
    *payload_len = PROTOCOL_DUMP_INFO_RSP_LEN;
    return PROTOCOL_OK;
}

protocol_status_t command_dump_read(command_t *cmd, const protocol_req_t *req,
                                    uint32_t *payload_len, const uint8_t **echo) {
    const uint32_t offset = get_le32(&req->data[REQ_OFFSET_AT]);
    const uint32_t len    = get_le32(&req->data[REQ_LEN_AT]);

    /* -4, not -3: the request LENGTH is a correct eight bytes, so what is
     * wrong is a value inside it and a host told -3 would look at the wrong
     * half of its frame. The cap is what keeps the answer inside cmd->chunk. */
    if ((len == 0U) || (len > PROTOCOL_DUMP_CHUNK_MAX)) {
        return PROTOCOL_ERR_BAD_ARG;
    }

    /* The driver bounds the range to the stored dump and tells absence apart
     * from a bad range, so this handler asks once and maps the answer - rather
     * than calling coredump_info_get() per chunk, which would recompute the
     * whole dump's checksum sixteen times to read it once. */
    const coredump_err_t err = coredump_read(offset, cmd->chunk, len);
    if (err == COREDUMP_ERR_NOT_FOUND) {
        /* Legal command, and the device is simply in no state to serve it. */
        return PROTOCOL_ERR_STATE;
    }
    if (err == COREDUMP_ERR_PARAM) {
        /* The range reaches past the stored dump. Past it there is only 0xFF
         * padding, which looks like data and is not. */
        return PROTOCOL_ERR_BAD_ARG;
    }
    if (err != COREDUMP_OK) {
        ESP_LOGE(TAG, "dump read %u at %u: %s", (unsigned)len, (unsigned)offset,
                 coredump_err_str(err));
        return PROTOCOL_ERR_HW;
    }

    /* Pointed at, never copied through the dispatcher's 32-byte stack buffer.
     * cmd->chunk outlives this call, which is what the frame builder needs. */
    *echo        = cmd->chunk;
    *payload_len = len;
    return PROTOCOL_OK;
}

protocol_status_t command_dump_erase(void) {
    const coredump_err_t err = coredump_erase();

    if (err != COREDUMP_OK) {
        ESP_LOGE(TAG, "dump erase: %s", coredump_err_str(err));
        return PROTOCOL_ERR_HW;
    }

    /* Erasing nothing succeeded, which is what makes a host's read-then-erase
     * safe to retry after a lost reply - and with
     * CONFIG_ESP_COREDUMP_FLASH_NO_OVERWRITE on, an erase that failed the
     * second time would leave the unit capturing no further panic at all. */
    return PROTOCOL_OK;
}

/* -------------------------- Private functions -------------------------- */

/* Arguments are checked at the public boundary (R-SRC-06); helpers assume it. */

/* Four lines rather than a shared helper, the same way command_upgrade.c and
 * protocol.c each keep their own: the wire is little-endian on a
 * little-endian part, so exporting this would create a dependency for nothing. */
static uint32_t get_le32(const uint8_t *in) {
    return (uint32_t)in[0] | ((uint32_t)in[1] << 8U) | ((uint32_t)in[2] << 16U) |
           ((uint32_t)in[3] << 24U);
}

static void put_le32(uint8_t *out, uint32_t value) {
    out[0] = (uint8_t)(value & 0xFFU);
    out[1] = (uint8_t)((value >> 8U) & 0xFFU);
    out[2] = (uint8_t)((value >> 16U) & 0xFFU);
    out[3] = (uint8_t)((value >> 24U) & 0xFFU);
}

/*** end of file ***/
