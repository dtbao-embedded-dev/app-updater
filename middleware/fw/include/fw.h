/**
 * @file    fw.h
 * @date    2026-09-06
 * @brief   Project-wide status code returned by every application and
 *          middleware function (R-ERR-08).
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

#ifndef FW_H
#define FW_H

/* ------------------------------ Includes ------------------------------- */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------- Types -------------------------------- */

/**
 * @brief   Status of any application or middleware call.
 *
 * `0` is success and every failure is negative (R-ERR-02), so `if (err !=
 * FW_OK)` reads the same everywhere. The codes in `-1 .. -19` are the generic
 * set and mean exactly the same thing in every module (R-ERR-03); `-20 .. -99`
 * is left free for a module that needs a failure of its own.
 *
 * A numeric value is never reused for a second meaning (R-ERR-05): retire a
 * code by leaving it defined and unused.
 *
 * A driver does not use this type — it converts the vendor status to its own
 * `<mod>_err_t` at the driver boundary (R-ERR-04, R-LAY-03).
 */
typedef enum {
    FW_OK              = 0,  /**< The call succeeded.                        */
    FW_ERR_PARAM       = -1, /**< Caller passed something impossible.        */
    FW_ERR_STATE       = -2, /**< Legal call, wrong lifecycle state.         */
    FW_ERR_TIMEOUT     = -3, /**< The peer or the bus did not answer in time.*/
    FW_ERR_NO_SPACE    = -4, /**< Caller's buffer or an internal queue full. */
    FW_ERR_UNSUPPORTED = -5, /**< The build or the hardware cannot do this.  */
    FW_ERR_NOT_FOUND   = -6, /**< The named record or partition is absent.   */
    FW_ERR_IO          = -7, /**< The transport or the flash itself failed.  */
    FW_ERR_CRC         = -8, /**< Stored or received data did not check out. */
    FW_ERR_NO_MEM      = -9, /**< An allocation the call needed failed.      */
} fw_err_t;

/* ------------------------ Public function prototypes ------------------- */

/**
 * @brief   Returns a short, stable name for a status code.
 * @param   err   any `fw_err_t`; an unknown value yields "FW_ERR_UNKNOWN"
 * @return  Pointer to a string literal with static lifetime. Never NULL, and
 *          the caller must not free it.
 * @note    Reentrant, callable from any task. Not from an ISR (R-LOG-02 —
 *          the caller logs the result).
 */
const char *fw_err_str(fw_err_t err);

/**
 * @brief   Reflected CRC-32 over a buffer, chainable.
 * @param   seed   0 to start, or a previous return value to continue over a
 *                 second buffer
 * @param   data   bytes to fold in; may be NULL only when `len` is 0
 * @param   len    number of bytes
 * @return  The CRC-32 of everything folded in so far.
 * @note    Polynomial 0xEDB88320, init and xorout 0xFFFFFFFF - the zlib and
 *          Ethernet CRC-32, and bit for bit what `esp_rom_crc32_le()` used to
 *          compute for this firmware. Deliberately plain C and no vendor call:
 *          it is checked against stored records and wire frames that outlive
 *          any one chip, so it may not depend on one chip's ROM.
 * @note    Chaining is exact - `f(f(0,a,n),b,m)` equals `f(0,ab,n+m)` - which
 *          is what lets the upgrade session fold an image chunk by chunk
 *          instead of re-reading the slot at the end.
 * @note    Reentrant, callable from any task and from an ISR.
 */
uint32_t fw_crc32_le(uint32_t seed, const void *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* FW_H */

/*** end of file ***/
