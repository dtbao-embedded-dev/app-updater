/**
 * @file    storage.h
 * @date    2026-09-07
 * @brief   Stores one opaque blob in non-volatile storage and hands it back.
 *          Knows nothing about what is in it.
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

#ifndef STORAGE_H
#define STORAGE_H

/* ------------------------------ Includes ------------------------------- */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------- Constants & macros ------------------------- */

/* Largest blob this module will store or read back.
 *
 * It exists because `storage_blob_save()` compares the new bytes against the
 * stored ones before writing (R-CFG-05), and that compare needs a buffer of a
 * size known at compile time — a VLA on a task stack is how a deep call chain
 * overflows one. 256 bytes is comfortably over the 176 the settings record
 * needs today; raise it here, in one place, when a caller needs more. */
#define STORAGE_BLOB_MAX 256U

/* -------------------------------- Types -------------------------------- */

/**
 * @brief   Status of a storage call.
 *
 * This module sits in the driver layer and must not include `fw.h`
 * (R-LAY-01), so it carries its own code space. The generic values
 * `-1 .. -19` mean exactly what they mean everywhere else (R-ERR-03), which
 * is what lets a caller map them onto `fw_err_t` one for one.
 */
typedef enum {
    STORAGE_OK            = 0,  /**< The call succeeded.                     */
    STORAGE_ERR_PARAM     = -1, /**< Caller passed something impossible.     */
    STORAGE_ERR_STATE     = -2, /**< Legal call, wrong lifecycle state.      */
    STORAGE_ERR_NO_SPACE  = -4, /**< Blob too big, or the medium is full.    */
    STORAGE_ERR_NOT_FOUND = -6, /**< Nothing has ever been written.          */
    STORAGE_ERR_IO        = -7, /**< The flash or the vendor SDK refused.    */
    STORAGE_ERR_CRC       = -8, /**< What is stored is not what this build
                                 *   writes - see `storage_blob_load()`.     */
    STORAGE_ERR_NO_MEM = -9,    /**< An allocation the call needed failed.   */
} storage_err_t;

/** @brief Configuration for one storage instance. */
typedef struct {
    const char *nvs_namespace; /**< NVS namespace; must outlive the instance. */
    const char *nvs_key;       /**< Blob key inside that namespace; must outlive it. */
} storage_cfg_t;

/** @brief Storage instance. Caller allocates; the module owns the contents. */
typedef struct {
    bool is_init;        /**< Lifecycle state, checked by every operation.   */
    uint32_t nvs_handle; /**< Vendor handle, opaque here to keep the SDK type
                          *   out of this header (R-LAY-03).                 */
    const char *nvs_key; /**< Borrowed from the config; not owned, not copied.*/
} storage_t;

/* ------------------------ Public function prototypes ------------------- */

/**
 * @brief   Returns a short, stable name for a storage status code.
 * @param   err   any `storage_err_t`; an unknown value yields
 *                "STORAGE_ERR_UNKNOWN"
 * @return  Pointer to a string literal with static lifetime; never NULL.
 * @note    Reentrant, callable from any task.
 */
const char *storage_err_str(storage_err_t err);

/**
 * @brief   Fills a config with the defaults this build was compiled with.
 * @return  By value; every field is set, none is left for the caller to guess.
 * @note    Reentrant, any task.
 */
storage_cfg_t storage_cfg_default(void);

/**
 * @brief   Brings the NVS subsystem up and opens the namespace the blob lives in.
 * @param   st    caller-allocated instance, zeroed by this call
 * @param   cfg   namespace and key; the strings must outlive `st`, they are
 *                not copied
 * @return  STORAGE_OK, STORAGE_ERR_PARAM on a NULL argument,
 *          STORAGE_ERR_STATE when already open, STORAGE_ERR_IO when NVS
 *          refused.
 * @note    **This owns the subsystem init**, so no caller has to know that the
 *          bytes end up in NVS. An NVS partition left unusable by a truncated
 *          or newer-format image is erased and re-initialized once, because a
 *          unit that cannot read its settings must still boot and take the
 *          defaults. Call once, from one task, before any other function here.
 */
storage_err_t storage_init(storage_t *st, const storage_cfg_t *cfg);

/**
 * @brief   Closes the handle `storage_init()` opened.
 * @param   st   instance, possibly only partly initialized
 * @return  STORAGE_OK, or STORAGE_ERR_PARAM when `st` is NULL.
 * @note    Safe to call twice and on a partly built instance (R-LFC-04). Does
 *          not de-initialize the NVS subsystem: another instance may still be
 *          using it.
 */
storage_err_t storage_deinit(storage_t *st);

/**
 * @brief   Reads the stored blob back, whatever it holds.
 * @param   st           initialized instance
 * @param[out] out       buffer to fill; untouched unless STORAGE_OK is returned
 * @param   cap          capacity of `out` in bytes
 * @param[out] out_len   bytes written, on STORAGE_OK
 * @return  STORAGE_OK; STORAGE_ERR_PARAM on a NULL argument or a zero `cap`;
 *          STORAGE_ERR_STATE before init; STORAGE_ERR_NOT_FOUND when nothing
 *          was ever written; STORAGE_ERR_CRC when the stored blob does not fit
 *          `cap`; STORAGE_ERR_NO_SPACE when `cap` is past STORAGE_BLOB_MAX;
 *          STORAGE_ERR_IO on an NVS failure.
 * @note    This module validates nothing about the content — the CRC and the
 *          layout version belong to whoever owns the shape. The one exception
 *          is the size, because NVS refuses to hand over a blob that does not
 *          fit and there is nothing else to report: a stored blob of a
 *          different length is not one this build wrote, so it comes back as
 *          STORAGE_ERR_CRC — "damaged" from the caller's point of view, which
 *          is the verdict that makes the caller fall back to its defaults
 *          rather than fail bring-up. One caller only.
 */
storage_err_t storage_blob_load(storage_t *st, void *out, size_t cap, size_t *out_len);

/**
 * @brief   Writes the blob, replacing whatever was there.
 * @param   st     initialized instance
 * @param   data   bytes to store; copied, not retained
 * @param   len    number of bytes, 1 .. STORAGE_BLOB_MAX
 * @return  STORAGE_OK, STORAGE_ERR_PARAM on a NULL argument or a zero `len`,
 *          STORAGE_ERR_STATE before init, STORAGE_ERR_NO_SPACE when `len` is
 *          past STORAGE_BLOB_MAX or the medium is full, STORAGE_ERR_IO on any
 *          other NVS failure.
 * @note    Skips the write when the stored bytes already match, so a caller
 *          may call it every cycle without wearing the sector out (R-CFG-05).
 *          Blocks on flash. One caller only; not callable from an ISR.
 */
storage_err_t storage_blob_save(storage_t *st, const void *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* STORAGE_H */

/*** end of file ***/
