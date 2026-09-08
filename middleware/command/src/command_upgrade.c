/**
 * @file    command_upgrade.c
 * @date    2026-09-06
 * @brief   The Upgrade range: a chunked image transfer into the slot the host
 *          names, verified whole before the slot is finalised.
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

/* ------------------------------ Includes ------------------------------- */

#include "command_priv.h"

#include "esp_log.h"

#include <string.h>

/* --------------------------- Private macros ---------------------------- */

/** Byte offsets inside UPG_BEGIN's 13-byte payload. */
#define BEGIN_TARGET    0U
#define BEGIN_IMG_SIZE  1U
#define BEGIN_IMG_CRC   5U
#define BEGIN_CHUNK_MAX 9U

/* ----------------------------- Static data ----------------------------- */

static const char *TAG = "command";

/* --------------------- Private function prototypes --------------------- */

static uint32_t get_le32(const uint8_t *in);
static void discard(command_t *cmd);
static protocol_status_t check_begin_args(const command_t *cmd, uint8_t target, uint32_t img_size,
                                          uint32_t chunk_max);
static protocol_status_t check_chunk_len(const command_upgrade_t *up, uint32_t chunk_len);

/* -------------------------- Public functions --------------------------- */

/* Arguments are checked at the public boundary (R-SRC-06); the map has already
 * pinned every LENGTH here, so a payload is the width the spec says. */

protocol_status_t command_upgrade_begin(command_t *cmd, const protocol_req_t *req) {
    const uint8_t target     = req->data[BEGIN_TARGET];
    const uint32_t img_size  = get_le32(&req->data[BEGIN_IMG_SIZE]);
    const uint32_t img_crc32 = get_le32(&req->data[BEGIN_IMG_CRC]);
    const uint32_t chunk_max = get_le32(&req->data[BEGIN_CHUNK_MAX]);

    const protocol_status_t bad = check_begin_args(cmd, target, img_size, chunk_max);
    if (bad != PROTOCOL_OK) {
        /* Nothing has been touched: no erase, and any session already open is
         * still open. A rejected UPG_BEGIN costs the host nothing but a retry. */
        return bad;
    }

    /* The source spec is explicit that this is where a leak would live: a host
     * retrying repeatedly would otherwise strand one session per attempt.
     * The old session goes before the new one is opened, never after. */
    discard(cmd);

    ota_session_t session = OTA_SESSION_NONE;
    const ota_err_t err   = ota_session_begin(target, img_size, &session);
    if (err != OTA_OK) {
        ESP_LOGE(TAG, "ota begin slot=%u size=%lu: %s", (unsigned)target, (unsigned long)img_size,
                 ota_err_str(err));
        return PROTOCOL_ERR_HW;
    }

    cmd->upgrade.is_open   = true;
    cmd->upgrade.target    = target;
    cmd->upgrade.img_size  = img_size;
    cmd->upgrade.img_crc32 = img_crc32;
    cmd->upgrade.chunk_max = chunk_max;
    cmd->upgrade.written   = 0U;
    cmd->upgrade.crc       = 0U;
    cmd->upgrade.session   = session;

    ESP_LOGI(TAG, "upgrade open: slot=%u size=%lu chunk=%lu", (unsigned)target,
             (unsigned long)img_size, (unsigned long)chunk_max);
    return PROTOCOL_OK;
}

protocol_status_t command_upgrade_write(command_t *cmd, const protocol_req_t *req) {
    /* LENGTH is PROTOCOL_LEN_ANY for this opcode, so the offset field is the
     * one width this handler has to check itself. */
    if (req->len < PROTOCOL_UPG_OFFSET_LEN) {
        return PROTOCOL_ERR_BAD_LEN;
    }
    if (!cmd->upgrade.is_open) {
        return PROTOCOL_ERR_STATE;
    }

    const uint32_t offset    = get_le32(&req->data[0]);
    const uint32_t chunk_len = req->len - PROTOCOL_UPG_OFFSET_LEN;

    /* Strictly sequential. The offset is on the wire so a host can prove it
     * did not lose its place, not so it can seek. */
    if (offset != cmd->upgrade.written) {
        ESP_LOGW(TAG, "upgrade offset %lu, expected %lu", (unsigned long)offset,
                 (unsigned long)cmd->upgrade.written);
        return PROTOCOL_ERR_STATE;
    }

    const protocol_status_t bad = check_chunk_len(&cmd->upgrade, chunk_len);
    if (bad != PROTOCOL_OK) {
        return bad;
    }

    const ota_err_t err =
        ota_session_write(cmd->upgrade.session, &req->data[PROTOCOL_UPG_OFFSET_LEN], chunk_len);
    if (err != OTA_OK) {
        ESP_LOGE(TAG, "ota write at %lu: %s", (unsigned long)offset, ota_err_str(err));
        return PROTOCOL_ERR_HW;
    }

    /* Folded as it goes, so UPG_END needs no second pass over 13 MB of flash. */
    cmd->upgrade.crc =
        fw_crc32_le(cmd->upgrade.crc, &req->data[PROTOCOL_UPG_OFFSET_LEN], chunk_len);
    cmd->upgrade.written += chunk_len;
    return PROTOCOL_OK;
}

protocol_status_t command_upgrade_end(command_t *cmd) {
    if (!cmd->upgrade.is_open) {
        return PROTOCOL_ERR_STATE;
    }

    /* Short of what the host declared is a transfer still in progress, which
     * is retryable - so -5, and the session stays open for the rest of it. */
    if (cmd->upgrade.written != cmd->upgrade.img_size) {
        ESP_LOGW(TAG, "upgrade end at %lu of %lu bytes", (unsigned long)cmd->upgrade.written,
                 (unsigned long)cmd->upgrade.img_size);
        return PROTOCOL_ERR_STATE;
    }

    if (cmd->upgrade.crc != cmd->upgrade.img_crc32) {
        /* The right number of bytes arrived and they are not the right bytes.
         * -6 rather than -4: the device cannot tell a wrong img_crc32 from a
         * corrupted transfer, and corruption is overwhelmingly the likelier
         * of the two. Either way the slot must not be finalised. */
        ESP_LOGE(TAG, "upgrade crc 0x%08lX, expected 0x%08lX", (unsigned long)cmd->upgrade.crc,
                 (unsigned long)cmd->upgrade.img_crc32);
        discard(cmd);
        return PROTOCOL_ERR_HW;
    }

    const uint8_t target = cmd->upgrade.target; /* read before the session goes */
    const ota_err_t err  = ota_session_end(cmd->upgrade.session);

    /* The driver releases the session whether it validated or not, so it is
     * gone either way and must not be aborted a second time. */
    memset(&cmd->upgrade, 0, sizeof(cmd->upgrade));

    if (err != OTA_OK) {
        ESP_LOGE(TAG, "ota end: %s", ota_err_str(err));
        return PROTOCOL_ERR_HW;
    }

    /* Finalised, and deliberately NOT armed. The host follows with Set
     * BOOT_SLOT and RESTART_APP; until it does, the device keeps running the
     * slot it booted from. */
    ESP_LOGI(TAG, "upgrade complete, slot %u valid and not armed", (unsigned)target);
    return PROTOCOL_OK;
}

/* -------------------------- Private functions -------------------------- */

static uint32_t get_le32(const uint8_t *in) {
    return (uint32_t)in[0] | ((uint32_t)in[1] << 8) | ((uint32_t)in[2] << 16) |
           ((uint32_t)in[3] << 24);
}

/* Throws away whatever session is open, leaving the slot unfinalised - which
 * is inert, because nothing but UPG_END makes a slot bootable. */
static void discard(command_t *cmd) {
    if (cmd->upgrade.is_open) {
        (void)ota_session_abort(cmd->upgrade.session);
    }
    memset(&cmd->upgrade, 0, sizeof(cmd->upgrade));
}

/* Every UPG_BEGIN argument, checked before anything is erased. */
static protocol_status_t check_begin_args(const command_t *cmd, uint8_t target, uint32_t img_size,
                                          uint32_t chunk_max) {
    if ((target != PROTOCOL_SLOT_UPDATER) && (target != PROTOCOL_SLOT_FIRMWARE)) {
        return PROTOCOL_ERR_BAD_ARG;
    }

    /* A slot this build does not have is a bad argument, not a fault. The size
     * comes back in the same call, which is what the fit check below needs. */
    uint32_t slot_size  = 0U;
    const ota_err_t err = ota_slot_size_get(target, &slot_size);
    if (err != OTA_OK) {
        return PROTOCOL_ERR_BAD_ARG;
    }

    /* Overwriting the image underneath the running code is exactly what the
     * two-slot layout exists to prevent. The wire's slot numbering and the
     * driver's are the same numbering - one pair of slots, one encoding. */
    uint8_t running = 0U;
    if (ota_running_slot_get(&running) != OTA_OK) {
        return PROTOCOL_ERR_HW;
    }
    if (running == target) {
        return PROTOCOL_ERR_STATE;
    }

    /* The other writer of this slot is the HTTP update cycle. It is asked, not
     * read, because it lives a layer above this module. */
    if ((cmd->cfg.is_busy != NULL) && cmd->cfg.is_busy(cmd->cfg.busy_ctx)) {
        return PROTOCOL_ERR_STATE;
    }

    /* An image that does not fit is refused here, minutes before the host
     * would otherwise find out. Zero is refused too: there is no such image,
     * and it would make the first chunk the last one. */
    if ((img_size == 0U) || (img_size > slot_size)) {
        return PROTOCOL_ERR_BAD_ARG;
    }

    /* The band, in 1024 B steps. Below the floor the per-frame overhead and
     * the round-trip status dominate; above the ceiling a whole UPG_WRITE
     * frame no longer fits PROTOCOL_MAX_DATA. */
    if ((chunk_max % PROTOCOL_UPG_CHUNK_STEP) != 0U) {
        return PROTOCOL_ERR_BAD_ARG;
    }
    if ((chunk_max < PROTOCOL_UPG_CHUNK_MIN) || (chunk_max > PROTOCOL_UPG_CHUNK_CAP)) {
        return PROTOCOL_ERR_BAD_ARG;
    }

    return PROTOCOL_OK;
}

/* The chunk rules, all of them length problems rather than value problems -
 * what is wrong is how much was sent, so the host shortens the next one. */
static protocol_status_t check_chunk_len(const command_upgrade_t *up, uint32_t chunk_len) {
    if (chunk_len == 0U) {
        return PROTOCOL_ERR_BAD_LEN;
    }
    if ((chunk_len > up->chunk_max) || (chunk_len > PROTOCOL_UPG_CHUNK_MAX)) {
        return PROTOCOL_ERR_BAD_LEN;
    }

    /* Running past the declared size is a length error, not a write: the host
     * has lost track of its own image. */
    const uint32_t remaining = up->img_size - up->written;
    if (chunk_len > remaining) {
        return PROTOCOL_ERR_BAD_LEN;
    }

    /* The last chunk carries the remainder and is exempt from the 1024 rule,
     * because an image size is not a multiple of 1024 and demanding one would
     * mean padding every image and teaching UPG_END to ignore the padding. The
     * device knows which chunk is last from img_size, so nothing says so on
     * the wire. */
    const bool is_last = (chunk_len == remaining);
    if (!is_last && ((chunk_len % PROTOCOL_UPG_CHUNK_STEP) != 0U)) {
        return PROTOCOL_ERR_BAD_LEN;
    }

    return PROTOCOL_OK;
}

/*** end of file ***/
