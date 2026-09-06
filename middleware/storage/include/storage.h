/**
 * @file    storage.h
 * @author  dtbao
 * @date    2026-09-06
 * @brief   Persists the updater's settings and boot record in NVS, versioned
 *          and CRC-protected.
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

#ifndef STORAGE_H
#define STORAGE_H

/* ------------------------------ Includes ------------------------------- */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fw.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------- Constants & macros ------------------------- */

#define STORAGE_VERSION_MAJOR 0
#define STORAGE_VERSION_MINOR 1
#define STORAGE_VERSION_PATCH 0

/** Record layout version. Bump it and add a migration step on any field change. */
#define STORAGE_RECORD_VERSION 1U

/** Buffer sizes, including the terminating NUL. */
#define STORAGE_URL_MAX     128U
#define STORAGE_VERSION_MAX 32U

/* -------------------------------- Types -------------------------------- */

/**
 * @brief   The one record this module reads and writes.
 *
 * `version` is first and `crc32` last, over everything above it (R-CFG-01,
 * R-CFG-02). `length` lets a newer firmware read a shorter old record.
 */
typedef struct {
    uint16_t version;                             /**< STORAGE_RECORD_VERSION when written. */
    uint16_t length;                              /**< sizeof() of the record as written.   */
    char manifest_url[STORAGE_URL_MAX];           /**< NUL-terminated HTTPS manifest URL.   */
    char last_ok_fw_version[STORAGE_VERSION_MAX]; /**< Last image confirmed healthy.     */
    uint32_t check_interval_ms;                   /**< Between update checks, 0 disables.   */
    uint32_t boot_fail_count;                     /**< Unconfirmed boots since last good.   */
    uint32_t crc32;                               /**< CRC-32 over the bytes above.         */
} storage_record_t;

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
 * @brief   Fills a config with the defaults this build was compiled with.
 * @return  By value; every field is set, none is left for the caller to guess.
 * @note    Reentrant, any task.
 */
storage_cfg_t storage_cfg_default(void);

/**
 * @brief   Fills a record with the compiled-in defaults for every setting.
 * @return  By value, CRC already correct. A device with erased flash boots on
 *          exactly this (R-CFG-03).
 * @note    Reentrant, any task.
 */
storage_record_t storage_record_default(void);

/**
 * @brief   Opens the NVS namespace the record lives in.
 * @param   st    caller-allocated instance, zeroed by this call
 * @param   cfg   namespace and key; the strings must outlive `st`, they are
 *                not copied
 * @return  FW_OK, FW_ERR_PARAM on a NULL argument, FW_ERR_STATE when already
 *          open, FW_ERR_IO when NVS refused.
 * @note    Call `nvs_flash_init()` before this. One caller only.
 */
fw_err_t storage_init(storage_t *st, const storage_cfg_t *cfg);

/**
 * @brief   Closes the NVS handle `storage_init()` opened.
 * @param   st   instance, possibly only partly initialized
 * @return  FW_OK, or FW_ERR_PARAM when `st` is NULL.
 * @note    Safe to call twice and on a partly built instance (R-LFC-04).
 */
fw_err_t storage_deinit(storage_t *st);

/**
 * @brief   Reads the record, validating its version, length and CRC.
 * @param   st          initialized instance
 * @param[out] out_rec  the stored record on FW_OK; untouched otherwise. The
 *                      caller owns the buffer.
 * @return  FW_OK, FW_ERR_PARAM on NULL, FW_ERR_STATE before init,
 *          FW_ERR_NOT_FOUND when nothing was ever written, FW_ERR_CRC when the
 *          stored bytes did not check out, FW_ERR_IO on an NVS failure.
 * @note    A caller that gets FW_ERR_CRC or FW_ERR_NOT_FOUND uses
 *          `storage_record_default()` — this function never guesses for it
 *          (R-CFG-02). One caller only.
 */
fw_err_t storage_record_load(storage_t *st, storage_record_t *out_rec);

/**
 * @brief   Writes the record, stamping its version, length and CRC first.
 * @param   st    initialized instance
 * @param   rec   record to store; copied, not retained. `version`, `length`
 *                and `crc32` are overwritten by this call.
 * @return  FW_OK, FW_ERR_PARAM on NULL, FW_ERR_STATE before init, FW_ERR_IO on
 *          an NVS failure.
 * @note    Skips the write when the stored bytes already match, so a caller
 *          may call it every cycle without wearing the sector out (R-CFG-05).
 *          Blocks on flash. One caller only; not callable from an ISR.
 */
fw_err_t storage_record_save(storage_t *st, const storage_record_t *rec);

#ifdef __cplusplus
}
#endif

#endif /* STORAGE_H */

/*** end of file ***/
