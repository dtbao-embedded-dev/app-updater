/**
 * @file    protocol.h
 * @date    2026-09-06
 * @brief   Wire format and command map of the USB command channel: frame
 *          codec, status codes, and the opcode table this build serves.
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

#ifndef PROTOCOL_H
#define PROTOCOL_H

/* ------------------------------ Includes ------------------------------- */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fw.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------- Constants & macros ------------------------- */

/* Direction magic, as a little-endian word. On the wire the request reads
 * `52 51 3E 3E` ("RQ>>") and the response `52 53 3C 3C` ("RS<<"): ASCII-ish so
 * a hex dump is readable, and differing in every direction-carrying byte so a
 * single flipped bit cannot turn one into the other. */
#define PROTOCOL_HDR_REQ 0x3E3E5152U
#define PROTOCOL_HDR_RSP 0x3C3C5352U

/** HEADER + COMMAND + LENGTH, the fixed part that comes before DATA. */
#define PROTOCOL_PREFIX_LEN 12U

/** Every byte of a frame that is not DATA: the prefix plus the CRC32. */
#define PROTOCOL_OVERHEAD_LEN 16U

/* Largest DATA a frame may carry. Sized to hold one 32 KB UPG_WRITE chunk plus
 * its 4-byte offset, which is what buys the fewest frames per image: a
 * 13.875 MB app_firmware is about 425 chunks at 32 KB. The price is static
 * RAM - this build spends two buffers of this order, not the three the source
 * spec budgets, because a reply body is built straight into the reply frame.
 * Tunable: lowering it lowers the accepted chunk band with it. */
#define PROTOCOL_MAX_DATA 32772U

/** Largest whole frame, so a caller sizes one buffer and never checks again. */
#define PROTOCOL_MAX_FRAME (PROTOCOL_OVERHEAD_LEN + PROTOCOL_MAX_DATA)

/** The 4-byte status that opens every response DATA, before its payload. */
#define PROTOCOL_STATUS_LEN 4U

/* Chunk band a host may propose in UPG_BEGIN, in 1024 B steps. Below the floor
 * the per-frame header, CRC and round-trip status dominate the transfer; above
 * the ceiling a whole UPG_WRITE frame no longer fits PROTOCOL_MAX_DATA. */
#define PROTOCOL_UPG_CHUNK_MIN  4096U
#define PROTOCOL_UPG_CHUNK_MAX  32768U
#define PROTOCOL_UPG_CHUNK_CAP  32768U
#define PROTOCOL_UPG_CHUNK_STEP 1024U

/** Fixed payload widths the spec pins. */
#define PROTOCOL_MAC_LEN           6U
#define PROTOCOL_VERSION_FIELD_LEN 16U
#define PROTOCOL_VERSION_LEN       32U
#define PROTOCOL_UPG_BEGIN_LEN     13U
#define PROTOCOL_UPG_OFFSET_LEN    4U

/** `req_len` of a command whose own handler decides what a legal length is. */
#define PROTOCOL_LEN_ANY UINT32_MAX

/* Opcodes this build serves. Every other opcode the spec defines lives in the
 * table in protocol.c as a number, because it needs no name here - it is only
 * ever answered PROTOCOL_ERR_UNSUPPORTED. */
#define PROTOCOL_CMD_RESTART_APP   0x0001U
#define PROTOCOL_CMD_PING          0x0006U
#define PROTOCOL_CMD_SET_BOOT_SLOT 0x0101U
#define PROTOCOL_CMD_GET_VERSION   0x0201U
#define PROTOCOL_CMD_GET_BOOT_SLOT 0x0202U
#define PROTOCOL_CMD_GET_WIFI_MAC  0x0203U
#define PROTOCOL_CMD_GET_BLE_MAC   0x0204U
#define PROTOCOL_CMD_UPG_BEGIN     0x0601U
#define PROTOCOL_CMD_UPG_WRITE     0x0602U
#define PROTOCOL_CMD_UPG_END       0x0603U

/** Boot slot encoding, shared by Set/Get BOOT_SLOT and UPG_BEGIN's `target`. */
#define PROTOCOL_SLOT_UPDATER  0U
#define PROTOCOL_SLOT_FIRMWARE 1U

/* -------------------------------- Types -------------------------------- */

/**
 * @brief   The 4-byte status that opens every response DATA.
 *
 * `0` is success and every failure is negative, the same shape as `fw_err_t`,
 * but these are wire values a host parses and must never be renumbered.
 *
 * `PROTOCOL_ERR_BAD_CMD` and `PROTOCOL_ERR_UNSUPPORTED` are the difference a
 * tool acts on: `-2` means this spec does not define the opcode at all - a
 * typo, or a tool built against a newer map - while `-7` means the opcode is
 * right and this firmware cannot serve it, so the fix is a different build and
 * not a different request. `PROTOCOL_ERR_CRC` is never transmitted: a request
 * whose own CRC fails is dropped without a reply, because its COMMAND cannot
 * be trusted enough to echo.
 */
typedef enum {
    PROTOCOL_OK      = 0,          /**< The command succeeded.               */
    PROTOCOL_ERR_CRC = -1,         /**< Never sent; a bad-CRC request is
                                    *   dropped silently.                    */
    PROTOCOL_ERR_BAD_CMD     = -2, /**< COMMAND is not in the spec at all.   */
    PROTOCOL_ERR_BAD_LEN     = -3, /**< LENGTH wrong for this command.       */
    PROTOCOL_ERR_BAD_ARG     = -4, /**< Payload value rejected.              */
    PROTOCOL_ERR_STATE       = -5, /**< Legal command, wrong device state.   */
    PROTOCOL_ERR_HW          = -6, /**< The driver underneath failed.        */
    PROTOCOL_ERR_UNSUPPORTED = -7, /**< Defined here, not built into this
                                    *   firmware.                            */
} protocol_status_t;

/** @brief What the command map knows about one opcode. */
typedef enum {
    PROTOCOL_CMD_SERVED = 0,  /**< This build has a handler for it.          */
    PROTOCOL_CMD_UNSUPPORTED, /**< The spec defines it; this build does not
                               *   serve it, so it answers -7.               */
} protocol_cmd_kind_t;

/**
 * @brief   One row of the command map.
 *
 * `req_len` is the exact REQ `LENGTH` the command takes, or
 * `PROTOCOL_LEN_ANY` when only the handler can judge it. Keeping the width
 * here is what lets the length check happen before the value check, so a tool
 * is told which half of its request to fix.
 */
typedef struct {
    uint32_t command;         /**< `0xRRNN` opcode.                          */
    uint32_t req_len;         /**< Exact REQ LENGTH, or PROTOCOL_LEN_ANY.    */
    protocol_cmd_kind_t kind; /**< Served here, or answered -7.              */
} protocol_cmd_info_t;

/**
 * @brief   One request the parser has accepted, CRC already verified.
 *
 * `data` points into the parser instance and stays valid only until the next
 * `protocol_parser_feed()` call on that instance. A handler that needs the
 * bytes for longer copies them.
 */
typedef struct {
    uint32_t command;    /**< `0xRRNN`, to be echoed in the response.        */
    uint32_t len;        /**< True payload length, 0 .. PROTOCOL_MAX_DATA.   */
    const uint8_t *data; /**< First `len` bytes of DATA; NULL when `len` is
                          *   0. The zero padding is not included.          */
} protocol_req_t;

/**
 * @brief   Byte-fed request parser. Caller allocates; the module owns it.
 *
 * Holds one frame under construction, so it costs `PROTOCOL_MAX_FRAME` of
 * whatever storage the caller put it in. Nothing is allocated on the receive
 * path: a frame that would not fit is rejected while its `LENGTH` is read.
 */
typedef struct {
    uint32_t len;                    /**< Bytes held in `buf`.               */
    uint32_t done;                   /**< Length of an accepted frame still
                                      *   sitting at the head of `buf`, so
                                      *   the request handed out stays
                                      *   readable until the next feed.      */
    uint8_t buf[PROTOCOL_MAX_FRAME]; /**< The frame being assembled.         */
} protocol_parser_t;

/* ------------------------ Public function prototypes ------------------- */

/**
 * @brief   Rounds a payload length up to the 4-byte boundary DATA is padded to.
 * @param   n   payload length, 0 .. PROTOCOL_MAX_DATA
 * @return  `n` rounded up to a multiple of 4. Every field of a frame is
 *          4-byte aligned, so the CRC32 always lands on a boundary.
 * @note    Reentrant, any context.
 */
static inline uint32_t protocol_round4(uint32_t n) {
    return (n + 3U) & ~3U;
}

/**
 * @brief   Returns a short, stable name for a wire status code.
 * @param   status   any `protocol_status_t`; an unknown value yields
 *                   "PROTOCOL_ERR_UNKNOWN"
 * @return  Pointer to a string literal with static lifetime; never NULL, and
 *          the caller must not free it.
 * @note    Reentrant, callable from any task. Not from an ISR (R-LOG-02 - the
 *          caller logs the result).
 */
const char *protocol_status_str(protocol_status_t status);

/**
 * @brief   Looks one opcode up in the command map.
 * @param   command   the `0xRRNN` word taken from a request
 * @return  The row, or NULL when the spec does not define this opcode - which
 *          the caller answers `PROTOCOL_ERR_BAD_CMD`. A row whose `kind` is
 *          `PROTOCOL_CMD_UNSUPPORTED` is answered
 *          `PROTOCOL_ERR_UNSUPPORTED` instead: the number is real, the handler
 *          is not built.
 * @note    Reentrant, any task. The returned row has static lifetime.
 */
const protocol_cmd_info_t *protocol_cmd_lookup(uint32_t command);

/**
 * @brief   Puts a parser back in its initial hunting state.
 * @param   parser   caller-allocated instance; its buffer is left alone
 * @note    Only the two lengths are cleared - the buffer is not wiped, because
 *          zeroing 32 KB to discard bytes that are about to be overwritten
 *          costs more than the bytes are worth. Nothing reads `buf` past
 *          `len`. Reentrant per instance, and safe on a parser that was
 *          mid-frame or has never been used.
 */
void protocol_parser_reset(protocol_parser_t *parser);

/**
 * @brief   Feeds the parser one received byte.
 *
 * The receiver never trusts `LENGTH` blindly. It hunts for the request magic
 * one byte at a time over anything that is not a frame; a `LENGTH` past
 * `PROTOCOL_MAX_DATA` abandons the frame and resumes hunting **from the byte
 * after the failed header**, so a corrupt length cannot make the parser
 * swallow the channel; and a CRC mismatch drops the frame with no response,
 * resuming the hunt the same way.
 *
 * @param   parser       initialized instance
 * @param   byte         the received byte
 * @param[out] out_req   filled in, and valid, only when this call returns
 *                       true; its `data` points into `parser`
 * @return  true when this byte completed a CRC-valid request, false otherwise.
 * @note    One caller per instance. Callable from an ISR only if nothing else
 *          touches the same instance; the reference wiring feeds it from a
 *          task instead.
 */
bool protocol_parser_feed(protocol_parser_t *parser, uint8_t byte, protocol_req_t *out_req);

/**
 * @brief   Builds a response frame, status first, ready to write to the wire.
 *
 * `RSP.DATA` is `[STATUS:4][payload]`, so the frame's `LENGTH` is
 * `PROTOCOL_STATUS_LEN + payload_len`. DATA is zero-padded to a multiple of 4
 * and the padding is covered by the CRC.
 *
 * @param[out] out        buffer the frame is written to
 * @param   out_cap       bytes available in `out`
 * @param   command       the request's COMMAND, echoed unchanged
 * @param   status        the 4-byte status opening DATA
 * @param   payload       response payload after the status; may be NULL when
 *                        `payload_len` is 0. Copied, not retained.
 * @param   payload_len   0 .. PROTOCOL_MAX_DATA - PROTOCOL_STATUS_LEN
 * @return  Bytes written, always a multiple of 4 and at least
 *          `PROTOCOL_OVERHEAD_LEN`; 0 when an argument is impossible or `out`
 *          is too small, in which case `out` is left untouched.
 * @note    Reentrant. Writes nothing on failure, so a caller may test the
 *          return value and send nothing.
 */
uint32_t protocol_rsp_build(uint8_t *out, uint32_t out_cap, uint32_t command,
                            protocol_status_t status, const uint8_t *payload, uint32_t payload_len);

#ifdef __cplusplus
}
#endif

#endif /* PROTOCOL_H */

/*** end of file ***/
