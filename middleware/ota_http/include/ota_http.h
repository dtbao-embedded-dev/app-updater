/**
 * @file    ota_http.h
 * @author  dtbao
 * @date    2026-09-06
 * @brief   Fetches a firmware image over HTTPS and hands it to the caller one
 *          chunk at a time.
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

#ifndef OTA_HTTP_H
#define OTA_HTTP_H

/* ------------------------------ Includes ------------------------------- */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fw.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------- Constants & macros ------------------------- */

#define OTA_HTTP_VERSION_MAJOR 0
#define OTA_HTTP_VERSION_MINOR 1
#define OTA_HTTP_VERSION_PATCH 0

/** Bytes handed to the chunk callback at a time. Tunable: larger costs RAM,
 *  smaller costs one callback per TCP segment. Measured, not guessed. */
#define OTA_HTTP_CHUNK_BYTES 1024U

/* -------------------------------- Types -------------------------------- */

/**
 * @brief   Receives one chunk of the image, in order, as it arrives.
 * @param   ctx    the `chunk_ctx` supplied at init
 * @param   data   chunk bytes; valid only for the duration of the call, the
 *                 callee must copy or consume them before returning
 * @param   len    1 .. OTA_HTTP_CHUNK_BYTES
 * @return  FW_OK to continue; any failure aborts the fetch and is returned
 *          unchanged by `ota_http_fetch()`.
 * @note    Runs in the task that called `ota_http_fetch()`, never in an ISR.
 *          It may block, and blocking here stalls the download.
 */
typedef fw_err_t (*ota_http_chunk_cb_t)(void *ctx, const uint8_t *data, size_t len);

/** @brief Configuration for one client instance. */
typedef struct {
    const char *url;              /**< HTTPS image URL; must outlive the instance. */
    const char *cert_pem;         /**< Server root cert, PEM, NUL-terminated. NULL
                                   *   uses the bundle compiled into the image. */
    ota_http_chunk_cb_t on_chunk; /**< Required; see the typedef for context.   */
    void *chunk_ctx;              /**< Passed back to `on_chunk` unchanged.     */
    uint32_t timeout_ms;          /**< Per-read budget; 0 is not allowed here.  */
} ota_http_cfg_t;

/** @brief Client instance. Caller allocates; the module owns the contents. */
typedef struct {
    bool is_init;            /**< Lifecycle state, checked by every operation. */
    bool is_running;         /**< A fetch is in progress on this instance.     */
    ota_http_cfg_t cfg;      /**< Copy of the config; the pointers are borrowed.*/
    void *client;            /**< Vendor handle, opaque here to keep the SDK
                              *   type out of this header (R-LAY-03).          */
    uint32_t received_bytes; /**< Bytes handed to the callback so far.        */
} ota_http_t;

/* ------------------------ Public function prototypes ------------------- */

/**
 * @brief   Fills a config with the defaults this build was compiled with.
 * @return  By value. `url`, `on_chunk` and `chunk_ctx` are left NULL — they
 *          have no sensible default and `ota_http_init()` rejects them.
 * @note    Reentrant, any task.
 */
ota_http_cfg_t ota_http_cfg_default(void);

/**
 * @brief   Builds the HTTPS client the fetch will use. No traffic yet.
 * @param   cli   caller-allocated instance, zeroed by this call
 * @param   cfg   copied into the instance; the strings it points at are
 *                borrowed and must outlive `cli`
 * @return  FW_OK, FW_ERR_PARAM on a NULL argument or a zero timeout,
 *          FW_ERR_STATE when already initialized, FW_ERR_NO_MEM when the
 *          client could not be built.
 * @note    One caller only. Split from the fetch so the application can bring
 *          every module up and check every status first (R-LFC-09).
 */
fw_err_t ota_http_init(ota_http_t *cli, const ota_http_cfg_t *cfg);

/**
 * @brief   Releases everything `ota_http_init()` claimed.
 * @param   cli   instance, possibly only partly initialized
 * @return  FW_OK, or FW_ERR_PARAM when `cli` is NULL.
 * @note    Safe to call twice and on a partly built instance (R-LFC-04). Not
 *          safe while a fetch is running — call it from the fetching task.
 */
fw_err_t ota_http_deinit(ota_http_t *cli);

/**
 * @brief   Downloads the image, calling `on_chunk` for each piece.
 * @param   cli   initialized instance
 * @return  FW_OK when the whole body was delivered, FW_ERR_PARAM on NULL,
 *          FW_ERR_STATE before init or while a fetch is already running,
 *          FW_ERR_TIMEOUT when a read exceeded `timeout_ms`, FW_ERR_IO on a
 *          transport or HTTP-status failure, or whatever `on_chunk` returned.
 * @note    Blocks the calling task for the whole download. One caller only;
 *          not callable from an ISR.
 */
fw_err_t ota_http_fetch(ota_http_t *cli);

/**
 * @brief   Reports how many bytes have reached the callback on this instance.
 * @param   cli             initialized instance
 * @param[out] out_bytes    bytes delivered since `ota_http_init()`
 * @return  FW_OK, FW_ERR_PARAM on NULL, FW_ERR_STATE before init.
 * @note    Readable from another task while a fetch runs; the value may be
 *          stale by the time it is read, which is what a progress bar wants.
 */
fw_err_t ota_http_progress_get(const ota_http_t *cli, uint32_t *out_bytes);

#ifdef __cplusplus
}
#endif

#endif /* OTA_HTTP_H */

/*** end of file ***/
