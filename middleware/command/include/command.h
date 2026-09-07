/**
 * @file    command.h
 * @date    2026-09-06
 * @brief   Dispatches one decoded USB command frame to the handler that
 *          serves it, and answers everything else with a status the host can
 *          act on.
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

#ifndef COMMAND_H
#define COMMAND_H

/* ------------------------------ Includes ------------------------------- */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bsp.h"
#include "fw.h"
#include "ota.h"
#include "protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------- Constants & macros ------------------------- */

/* How long RESTART_APP waits after its reply before resetting. The reply has
 * already been flushed to the USB FIFO by then, but flushed is not the same as
 * read: the host still has to be scheduled and issue an IN transfer. A frame
 * this small is gone in well under a millisecond, so the wait is generous by
 * two orders of magnitude and costs nothing that matters before a reboot. */
#define COMMAND_RESTART_GRACE_MS 100U

/* -------------------------------- Types -------------------------------- */

/**
 * @brief   Writes one built response frame to the wire.
 * @param   ctx    the `reply_ctx` supplied at init
 * @param   data   the whole frame, header to CRC
 * @param   len    frame length in bytes
 * @return  FW_OK when the frame was handed to the transport; any failure is
 *          logged by the caller and otherwise ignored, because there is
 *          nothing useful to tell a host that is not listening.
 * @note    Runs in the task that called `command_on_frame()`. It may block for
 *          as long as the transport takes.
 */
typedef fw_err_t (*command_reply_cb_t)(void *ctx, const uint8_t *data, size_t len);

/**
 * @brief   Reports whether something else is already writing an OTA slot.
 * @param   ctx   the `busy_ctx` supplied at init
 * @return  true when a slot write is in progress elsewhere, so an incoming
 *          UPG_BEGIN must be refused with `PROTOCOL_ERR_STATE`.
 * @note    Asked, not read directly, because the thing that knows is the
 *          update cycle in `application/` and this module sits below it -
 *          nothing here may include a header from a layer above (R-LAY-01).
 *          Runs in the dispatch task and must not block.
 */
typedef bool (*command_busy_cb_t)(void *ctx);

/** @brief Configuration for the one dispatcher instance. */
typedef struct {
    command_reply_cb_t on_reply; /**< Required; see the typedef.            */
    void *reply_ctx;             /**< Passed back to `on_reply` unchanged.  */
    command_busy_cb_t is_busy;   /**< Optional; NULL means never busy.      */
    void *busy_ctx;              /**< Passed back to `is_busy` unchanged.   */
} command_cfg_t;

/**
 * @brief   One in-flight image transfer.
 *
 * The fields are the module's business; they sit in the public struct only
 * because the caller allocates the instance. `session` is the driver's own
 * opaque type, so no SDK type reaches this header (R-LAY-03) and no caller has
 * anything to do with the value but hand it back.
 *
 * There is no abort opcode, so a session ends exactly three ways: UPG_END
 * finalises it, the next UPG_BEGIN throws it away, or a reboot forgets it. All
 * three are safe, because an unfinished slot is never armed - only UPG_END,
 * after the whole-image CRC matches, touches anything a bootloader reads.
 */
typedef struct {
    bool is_open;          /**< A transfer is in progress.                   */
    uint8_t target;        /**< Slot being written, PROTOCOL_SLOT_*.         */
    uint32_t img_size;     /**< Total image bytes the host declared.         */
    uint32_t img_crc32;    /**< CRC-32 the finished image must match.        */
    uint32_t chunk_max;    /**< Accepted chunk size, enforced every write.   */
    uint32_t written;      /**< Bytes written, and the next offset expected. */
    uint32_t crc;          /**< Running CRC-32 over what has been written.   */
    ota_session_t session; /**< The open write session, opaque here.         */
} command_upgrade_t;

/**
 * @brief   Dispatcher instance. Caller allocates; the module owns the contents.
 *
 * Carries the response frame buffer, which is the second of the two
 * `PROTOCOL_MAX_FRAME` buffers this channel costs - the first being the
 * parser's. A response body is built straight into it rather than into a third.
 */
typedef struct {
    bool is_init;                      /**< Lifecycle state, checked by ops. */
    command_cfg_t cfg;                 /**< Copy of the config.              */
    command_upgrade_t upgrade;         /**< The in-flight transfer, if any.  */
    uint8_t frame[PROTOCOL_MAX_FRAME]; /**< The response being built.        */
} command_t;

/* ------------------------ Public function prototypes ------------------- */

/**
 * @brief   Validates the config and readies the dispatcher. No traffic yet.
 * @param   cmd   caller-allocated instance, zeroed by this call
 * @param   cfg   copied into the instance; `on_reply` is required
 * @return  FW_OK, FW_ERR_PARAM on a NULL argument or a NULL `on_reply`,
 *          FW_ERR_STATE when already initialized.
 * @note    One caller only (R-LFC-09).
 */
fw_err_t command_init(command_t *cmd, const command_cfg_t *cfg);

/**
 * @brief   Releases everything `command_init()` claimed.
 * @param   cmd   instance, possibly only partly initialized
 * @return  FW_OK, or FW_ERR_PARAM when `cmd` is NULL.
 * @note    Repeatable and safe on a partly built instance (R-LFC-04).
 */
fw_err_t command_deinit(command_t *cmd);

/**
 * @brief   Serves one decoded request and sends exactly one response.
 *
 * The order of the checks is the contract, not an implementation detail. An
 * opcode the spec never defined is `PROTOCOL_ERR_BAD_CMD`; one it defines that
 * this firmware cannot serve is `PROTOCOL_ERR_UNSUPPORTED`; a `LENGTH` that
 * does not match the opcode's width is `PROTOCOL_ERR_BAD_LEN` and the handler
 * never runs. Only then does a value get looked at, so a host is always told
 * which half of its request to fix.
 *
 * @param   cmd   initialized instance
 * @param   req   a request the parser has already CRC-checked
 * @return  FW_OK when a response was built and handed to the transport,
 *          FW_ERR_PARAM on a NULL argument, FW_ERR_STATE before init, or
 *          whatever `on_reply` returned.
 * @note    Always answers, for every input except a NULL argument - a host
 *          that gets silence cannot tell a refused command from a dead cable.
 *          RESTART_APP is the one handler that does not return: it replies,
 *          waits `COMMAND_RESTART_GRACE_MS`, and resets the chip. Blocks for
 *          as long as the work it starts; one caller only, not from an ISR.
 */
fw_err_t command_on_frame(command_t *cmd, const protocol_req_t *req);

#ifdef __cplusplus
}
#endif

#endif /* COMMAND_H */

/*** end of file ***/
