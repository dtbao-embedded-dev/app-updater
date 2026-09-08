/**
 * @file    cfg.h
 * @date    2026-09-07
 * @brief   The device's settings: one validated get/set pair per setting, and a
 *          caller-supplied adapter that puts the record somewhere.
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

#ifndef CFG_H
#define CFG_H

/* ------------------------------ Includes ------------------------------- */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fw.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------- Constants & macros ------------------------- */

/** Record layout version. Bump it and add a migration step on any field change. */
#define CFG_RECORD_VERSION 1U

/** Buffer sizes, including the terminating NUL. */
#define CFG_URL_MAX     128U
#define CFG_VERSION_MAX 32U

/* Ceiling on `check_interval_ms`, exclusive.
 *
 * This is half the uint32 millisecond range (~24.8 days), and it is here
 * because of a constraint that lives a layer ABOVE this module: the update
 * cycle schedules by comparing a wrapped difference against that same half
 * range, so an interval at or past it reads as "already due" on every step
 * (`UPDATER_HORIZON_MS` in `application/updater/src/updater.c`).
 *
 * The number is duplicated rather than shared because `middleware/` may not
 * include a header from `application/` (R-LAY-01), and validating the value
 * where it is set beats failing bring-up minutes later. Change one and you must
 * change the other: nothing goes red to tell you. */
#define CFG_CHECK_INTERVAL_MAX_MS 0x80000000U

/* -------------------------------- Types -------------------------------- */

/**
 * @brief   Reads the stored record back, however the caller stores it.
 * @param   ctx           the `ctx` supplied in `cfg_store_t`
 * @param[out] out        buffer to fill; the callee writes at most `cap` bytes
 * @param   cap           capacity of `out` in bytes
 * @param[out] out_len    bytes actually written, on FW_OK
 * @return  FW_OK; FW_ERR_NOT_FOUND when nothing was ever stored; FW_ERR_CRC
 *          when the stored bytes did not check out at the storage layer. Both
 *          of those make `cfg_init()` fall back to the compiled-in defaults.
 *          Any other failure is returned by `cfg_init()` unchanged.
 * @note    Runs in the task that called `cfg_init()`. May block on flash.
 */
typedef fw_err_t (*cfg_load_cb_t)(void *ctx, void *out, size_t cap, size_t *out_len);

/**
 * @brief   Persists the record, however the caller stores it.
 * @param   ctx    the `ctx` supplied in `cfg_store_t`
 * @param   data   the whole record; valid only for the duration of the call
 * @param   len    record length in bytes
 * @return  FW_OK, or any failure, which `cfg_save()` returns unchanged.
 * @note    Runs in the task that called `cfg_save()`. May block on flash, and
 *          is expected to skip the write when the stored bytes already match.
 */
typedef fw_err_t (*cfg_save_cb_t)(void *ctx, const void *data, size_t len);

/**
 * @brief   Where the record goes. Both callbacks are required.
 *
 * This struct is the whole reason the module knows nothing about NVS, a raw
 * partition, or a file: persistence is asked for, not performed here. The same
 * shape `ota_http` uses to report progress upward without naming its listener.
 */
typedef struct {
    cfg_load_cb_t load; /**< Required; see the typedef.               */
    cfg_save_cb_t save; /**< Required; see the typedef.               */
    void *ctx;          /**< Passed back to both callbacks unchanged. */
} cfg_store_t;

/**
 * @brief   The one record this module reads and writes.
 *
 * `version` is first and `crc32` last, over everything above it (R-CFG-01,
 * R-CFG-02). `length` lets a newer firmware recognise a shorter old record.
 *
 * The fields are the module's business; they sit in a public struct only
 * because the caller allocates the instance, the same reason
 * `command_upgrade_t` does. Read and write them through the accessors below,
 * which are the only things that validate.
 */
typedef struct {
    uint16_t version;                         /**< CFG_RECORD_VERSION when written.  */
    uint16_t length;                          /**< sizeof() of the record as written.*/
    char manifest_url[CFG_URL_MAX];           /**< NUL-terminated HTTPS manifest URL.*/
    char last_ok_fw_version[CFG_VERSION_MAX]; /**< Last image confirmed healthy.     */
    uint32_t check_interval_ms;               /**< Between update checks, 0 disables.*/
    uint32_t boot_fail_count;                 /**< Unconfirmed boots since last good.*/
    uint32_t crc32;                           /**< CRC-32 over the bytes above.      */
} cfg_record_t;

/** @brief Settings instance. Caller allocates; the module owns the contents. */
typedef struct {
    bool is_init;      /**< Lifecycle state, checked by every operation. */
    cfg_store_t store; /**< Copy of the adapter; `ctx` is borrowed.      */
    cfg_record_t rec;  /**< The live record, valid once `is_init`.       */
} cfg_t;

/* ------------------------ Public function prototypes ------------------- */

/**
 * @brief   Fills a record with the compiled-in defaults for every setting.
 * @return  By value, CRC already correct. A device with erased storage runs on
 *          exactly this (R-CFG-03).
 * @note    Reentrant, any task. Exposed so a caller can compare against the
 *          shipped defaults without opening an instance.
 */
cfg_record_t cfg_record_default(void);

/**
 * @brief   Loads the record through the adapter, or falls back to defaults.
 * @param   c       caller-allocated instance, zeroed by this call
 * @param   store   adapter; copied into the instance, and `ctx` must outlive it
 * @return  FW_OK when the instance is usable — including when the stored record
 *          was missing, damaged or out of range and the defaults were taken
 *          instead (R-CFG-02, R-CFG-03). FW_ERR_PARAM on a NULL argument or a
 *          NULL callback, FW_ERR_STATE when already initialized, and any other
 *          failure the adapter's `load` reported, unchanged.
 * @note    One caller only (R-LFC-09). Blocks for as long as `load` does.
 */
fw_err_t cfg_init(cfg_t *c, const cfg_store_t *store);

/**
 * @brief   Releases everything `cfg_init()` claimed.
 * @param   c   instance, possibly only partly initialized
 * @return  FW_OK, or FW_ERR_PARAM when `c` is NULL.
 * @note    Safe to call twice and on a partly built instance (R-LFC-04).
 */
fw_err_t cfg_deinit(cfg_t *c);

/**
 * @brief   Resets every setting to its compiled-in default, in RAM only.
 * @param   c   initialized instance
 * @return  FW_OK, FW_ERR_PARAM on NULL, FW_ERR_STATE before init.
 * @note    Nothing is written until `cfg_save()`. That split is deliberate: a
 *          factory reset that reboots before it saves must leave the old
 *          record readable.
 */
fw_err_t cfg_defaults_set(cfg_t *c);

/**
 * @brief   Hands the whole record to the adapter, stamped and CRC'd.
 * @param   c   initialized instance
 * @return  FW_OK, FW_ERR_PARAM on NULL, FW_ERR_STATE before init, or whatever
 *          the adapter's `save` returned.
 * @note    A setter never calls this — the caller decides when a batch of
 *          changes is worth a flash write. One caller only; not from an ISR.
 */
fw_err_t cfg_save(cfg_t *c);

/**
 * @brief   Copies the manifest URL out.
 * @param   c        initialized instance
 * @param[out] out   buffer for the NUL-terminated URL
 * @param   cap      capacity of `out`; must hold the string and its NUL
 * @return  FW_OK, FW_ERR_PARAM on NULL, FW_ERR_STATE before init,
 *          FW_ERR_NO_SPACE when `cap` is too small — `out` is untouched then.
 * @note    Readable from another task; the value may be stale on return.
 */
fw_err_t cfg_manifest_url_get(const cfg_t *c, char *out, size_t cap);

/**
 * @brief   Sets the manifest URL. Nothing is written until `cfg_save()`.
 * @param   c     initialized instance
 * @param   url   NUL-terminated URL, at most CFG_URL_MAX - 1 characters
 * @return  FW_OK, FW_ERR_STATE before init, FW_ERR_PARAM on a NULL argument, an
 *          empty string, or one that does not fit — the stored value survives.
 */
fw_err_t cfg_manifest_url_set(cfg_t *c, const char *url);

/**
 * @brief   Copies the last image version confirmed healthy out.
 * @param   c        initialized instance
 * @param[out] out   buffer for the NUL-terminated version; "" when none yet
 * @param   cap      capacity of `out`; must hold the string and its NUL
 * @return  FW_OK, FW_ERR_PARAM on NULL, FW_ERR_STATE before init,
 *          FW_ERR_NO_SPACE when `cap` is too small — `out` is untouched then.
 */
fw_err_t cfg_last_ok_fw_version_get(const cfg_t *c, char *out, size_t cap);

/**
 * @brief   Records the image version that just proved itself healthy.
 * @param   c         initialized instance
 * @param   version   NUL-terminated version, at most CFG_VERSION_MAX - 1
 *                    characters; "" is allowed and means "none yet"
 * @return  FW_OK, FW_ERR_STATE before init, FW_ERR_PARAM on NULL or a string
 *          that does not fit — the stored value survives.
 */
fw_err_t cfg_last_ok_fw_version_set(cfg_t *c, const char *version);

/**
 * @brief   Reads the interval between update checks.
 * @param   c           initialized instance
 * @param[out] out_ms   milliseconds; 0 means checking is disabled
 * @return  FW_OK, FW_ERR_PARAM on NULL, FW_ERR_STATE before init.
 */
fw_err_t cfg_check_interval_ms_get(const cfg_t *c, uint32_t *out_ms);

/**
 * @brief   Sets the interval between update checks.
 * @param   c    initialized instance
 * @param   ms   0 .. CFG_CHECK_INTERVAL_MAX_MS - 1; 0 disables checking
 *               entirely, which is a real setting rather than a missing one
 *               (R-CFG-03)
 * @return  FW_OK, FW_ERR_STATE before init, FW_ERR_PARAM on NULL or at/past
 *          CFG_CHECK_INTERVAL_MAX_MS — the stored value survives.
 * @note    That ceiling is the update cycle's scheduling horizon; see the macro.
 */
fw_err_t cfg_check_interval_ms_set(cfg_t *c, uint32_t ms);

/**
 * @brief   Reads the count of boots that never confirmed themselves.
 * @param   c              initialized instance
 * @param[out] out_count   the stored count
 * @return  FW_OK, FW_ERR_PARAM on NULL, FW_ERR_STATE before init.
 */
fw_err_t cfg_boot_fail_count_get(const cfg_t *c, uint32_t *out_count);

/**
 * @brief   Sets the count of boots that never confirmed themselves.
 * @param   c       initialized instance
 * @param   count   any value; every uint32 is a legal count
 * @return  FW_OK, FW_ERR_PARAM on NULL, FW_ERR_STATE before init.
 */
fw_err_t cfg_boot_fail_count_set(cfg_t *c, uint32_t count);

#ifdef __cplusplus
}
#endif

#endif /* CFG_H */

/*** end of file ***/
