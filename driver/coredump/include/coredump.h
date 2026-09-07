/**
 * @file    coredump.h
 * @date    2026-09-07
 * @brief   The core dump contract: whether the last panic left a dump behind,
 *          how to read its bytes back, how to clear it, and the panic reason
 *          as text.
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

#ifndef COREDUMP_H
#define COREDUMP_H

/* ------------------------------ Includes ------------------------------- */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------- Constants & macros ------------------------- */

/**
 * Capacity a panic-reason buffer needs, NUL included. The vendor builds that
 * string from the exception name, the faulting core and the address, so the
 * width is the SDK's business rather than ours — this is simply enough for
 * every reason it produces, so a caller that passes this much never sees a
 * truncated answer.
 */
#define COREDUMP_REASON_MAX 200U

/* -------------------------------- Types -------------------------------- */

/**
 * @brief   Status of a core dump call.
 *
 * This module sits in the driver layer and must not include `fw.h`
 * (R-LAY-01), so it carries its own code space. The generic values
 * `-1 .. -19` mean exactly what they mean everywhere else (R-ERR-03), which
 * is what lets a caller map them onto `fw_err_t` one for one.
 *
 * There is deliberately no `COREDUMP_ERR_CORRUPT`: a damaged dump is not a
 * failed call, it is an answer — `coredump_info_get()` reports it through
 * `coredump_state_t` and the bytes stay readable, because a dump you cannot
 * checksum is exactly the one worth looking at.
 */
typedef enum {
    COREDUMP_OK            = 0,  /**< The call succeeded.                   */
    COREDUMP_ERR_PARAM     = -1, /**< Caller passed something impossible.   */
    COREDUMP_ERR_NOT_FOUND = -6, /**< No dump stored, or no reason in it.    */
    COREDUMP_ERR_IO        = -7, /**< The flash or the vendor SDK refused.   */
} coredump_err_t;

/**
 * @brief   What the core dump partition currently holds.
 *
 * These values go **on the wire unchanged** as the `state` byte of a
 * `DUMP_INFO` response. One encoding for one fact: two would be a bug waiting
 * to happen, the same reason slot numbers are not translated either.
 *
 * `COREDUMP_CORRUPT` is worth its own value rather than being folded into
 * `COREDUMP_ABSENT`: "nothing ever crashed" and "something crashed and the
 * record is damaged" send a reader to completely different places.
 */
typedef enum {
    COREDUMP_ABSENT  = 0, /**< The partition is blank; no panic wrote it.    */
    COREDUMP_VALID   = 1, /**< A dump is stored and its checksum matches.    */
    COREDUMP_CORRUPT = 2, /**< A dump is stored and its checksum does not.   */
} coredump_state_t;

/* ------------------------ Public function prototypes ------------------- */

/**
 * @brief   Returns a short, stable name for a core dump status code.
 * @param   err   any `coredump_err_t`; an unknown value yields
 *                "COREDUMP_ERR_UNKNOWN"
 * @return  Pointer to a string literal with static lifetime; never NULL.
 * @note    Reentrant, callable from any task.
 */
const char *coredump_err_str(coredump_err_t err);

/**
 * @brief   Reports what the core dump partition holds, and how much of it.
 * @param   out_state   receives `COREDUMP_ABSENT`, `_VALID` or `_CORRUPT`
 * @param   out_size    receives the stored image length in bytes, checksum
 *                      included; `0` when the state is `COREDUMP_ABSENT`
 * @return  COREDUMP_OK — including for an absent dump, which is an answer and
 *          not a failure — COREDUMP_ERR_PARAM on a NULL argument, or
 *          COREDUMP_ERR_IO when the partition cannot be reached at all.
 * @note    Verifying the checksum reads the whole dump, so this call costs a
 *          flash read of `*out_size` bytes rather than a constant. Blocks on
 *          flash; not callable from an ISR.
 */
coredump_err_t coredump_info_get(coredump_state_t *out_state, uint32_t *out_size);

/**
 * @brief   Reads raw bytes out of the stored dump.
 * @param   offset   byte offset from the start of the dump image
 * @param   out      buffer receiving `len` bytes
 * @param   len      how many bytes to read; must be non-zero
 * @return  COREDUMP_OK, COREDUMP_ERR_PARAM on a NULL `out`, a zero `len`, or a
 *          range reaching past the partition, COREDUMP_ERR_NOT_FOUND when no
 *          dump is stored, COREDUMP_ERR_IO when the read fails.
 * @note    The checksum is **not** checked here, so a corrupt dump reads back
 *          fine. Bounding a read to the dump's own length is the caller's job
 *          (this only refuses to leave the partition), because a caller that
 *          wants the trailing blank space is asking a legitimate question.
 *          Blocks on flash; not callable from an ISR.
 */
coredump_err_t coredump_read(uint32_t offset, void *out, uint32_t len);

/**
 * @brief   Clears the stored dump, freeing the partition for the next panic.
 * @return  COREDUMP_OK — including when there was nothing to erase, so a
 *          caller retrying after a lost reply cannot fail on the second try —
 *          or COREDUMP_ERR_IO when the erase fails.
 * @note    Erases every sector of the partition, so it costs the whole
 *          partition erase time. Blocks on flash; not callable from an ISR.
 */
coredump_err_t coredump_erase(void);

/**
 * @brief   Reads the last panic reason out of the stored dump, as text.
 * @param   out   buffer receiving a NUL-terminated string
 * @param   cap   capacity of `out`; `COREDUMP_REASON_MAX` always suffices
 * @return  COREDUMP_OK, COREDUMP_ERR_PARAM on a NULL `out` or a zero `cap`,
 *          COREDUMP_ERR_NOT_FOUND when no dump is stored or the dump carries
 *          no reason, COREDUMP_ERR_IO when the read fails.
 * @note    Already-readable text, so nothing has to be symbolised to log it —
 *          which is the whole reason this exists next to `coredump_read()`.
 *          Blocks on flash; not callable from an ISR.
 */
coredump_err_t coredump_reason_get(char *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* COREDUMP_H */

/*** end of file ***/
