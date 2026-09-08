/**
 * @file    ota.h
 * @date    2026-09-07
 * @brief   The OTA slot contract: which slot runs, what each one holds, how a
 *          new image is written into one, and how a fresh boot is confirmed.
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

#ifndef OTA_H
#define OTA_H

/* ------------------------------ Includes ------------------------------- */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------- Constants & macros ------------------------- */

/**
 * How many app slots this driver addresses. Slots are numbered from 0 in the
 * order the partition table declares them, which on this product means slot 0
 * is `app_updater` and slot 1 is `app_firmware` — but the driver is told an
 * index and knows nothing about what either slot is for (R-LAY-06).
 */
#define OTA_SLOT_COUNT 2U

/**
 * Capacity a version buffer needs, NUL included. Sized to the widest version
 * field an image header can carry, so a caller that passes this much never
 * sees a truncated answer.
 */
#define OTA_VERSION_MAX 32U

/** Value of an `ota_session_t` that names no session. */
#define OTA_SESSION_NONE 0U

/* -------------------------------- Types -------------------------------- */

/**
 * @brief   Status of an OTA call.
 *
 * This module sits in the driver layer and must not include `fw.h`
 * (R-LAY-01), so it carries its own code space. The generic values
 * `-1 .. -19` mean exactly what they mean everywhere else (R-ERR-03), which
 * is what lets a caller map them onto `fw_err_t` one for one.
 */
typedef enum {
    OTA_OK            = 0,  /**< The call succeeded.                        */
    OTA_ERR_PARAM     = -1, /**< Caller passed something impossible.        */
    OTA_ERR_STATE     = -2, /**< Legal call, wrong lifecycle state.         */
    OTA_ERR_NO_SPACE  = -4, /**< The image does not fit the slot.           */
    OTA_ERR_NOT_FOUND = -6, /**< No such slot, or it holds no valid image.  */
    OTA_ERR_IO        = -7, /**< The flash or the vendor SDK refused.       */
} ota_err_t;

/**
 * @brief   An open write session, opaque to the caller.
 *
 * A vendor handle behind a plain integer: `esp_ota_handle_t` may not appear
 * above the driver layer (R-LAY-03), and a caller has nothing to do with the
 * value but hand it back.
 */
typedef uint32_t ota_session_t;

/* ------------------------ Public function prototypes ------------------- */

/**
 * @brief   Returns a short, stable name for an OTA status code.
 * @param   err   any `ota_err_t`; an unknown value yields "OTA_ERR_UNKNOWN"
 * @return  Pointer to a string literal with static lifetime; never NULL.
 * @note    Reentrant, callable from any task.
 */
const char *ota_err_str(ota_err_t err);

/**
 * @brief   Reports how many bytes a slot can hold.
 * @param   slot       0 .. `OTA_SLOT_COUNT` - 1
 * @param   out_size   receives the slot size in bytes; untouched on failure
 * @return  OTA_OK, OTA_ERR_PARAM on a bad slot number or a NULL pointer,
 *          OTA_ERR_NOT_FOUND when this build's partition table has no such
 *          slot.
 * @note    Answered from the partition table, so it costs no flash read.
 */
ota_err_t ota_slot_size_get(uint8_t slot, uint32_t *out_size);

/**
 * @brief   Reads the version string out of a slot's image header.
 * @param   slot   0 .. `OTA_SLOT_COUNT` - 1
 * @param   out    receives a NUL-terminated version, truncated to fit
 * @param   cap    capacity of `out`, at least 1; `OTA_VERSION_MAX` never
 *                 truncates
 * @return  OTA_OK, OTA_ERR_PARAM on a bad argument, OTA_ERR_NOT_FOUND when the
 *          slot is absent or holds no image whose header can be read.
 * @note    A slot that was never written is `OTA_ERR_NOT_FOUND`, not an empty
 *          string: "never written" and "written with an empty version" are
 *          different facts and the caller may care which.
 */
ota_err_t ota_slot_version_get(uint8_t slot, char *out, size_t cap);

/**
 * @brief   Reports which slot the running image was booted from.
 * @param   out_slot   receives 0 .. `OTA_SLOT_COUNT` - 1
 * @return  OTA_OK, OTA_ERR_PARAM when `out_slot` is NULL, OTA_ERR_STATE when
 *          the bootloader started something that is not an app slot at all.
 */
ota_err_t ota_running_slot_get(uint8_t *out_slot);

/**
 * @brief   Reads the version string of the running image.
 * @param   out   receives a NUL-terminated version, truncated to fit
 * @param   cap   capacity of `out`, at least 1
 * @return  OTA_OK, OTA_ERR_PARAM on a bad argument, OTA_ERR_IO when the SDK
 *          cannot produce the running image's description.
 * @note    Comes from the image header compiled into this binary, so it is the
 *          same number `VERSION` fed the build (R-VER-02).
 */
ota_err_t ota_running_version_get(char *out, size_t cap);

/**
 * @brief   Arms a slot for the NEXT boot.
 * @param   slot   0 .. `OTA_SLOT_COUNT` - 1
 * @return  OTA_OK, OTA_ERR_PARAM on a bad slot number, OTA_ERR_NOT_FOUND when
 *          the slot is absent, OTA_ERR_STATE when the slot holds no image that
 *          validates.
 * @note    Writes the boot selection and returns; it does not reset. Nothing
 *          here checks that the image is one you want — only that it is one
 *          the bootloader will accept.
 */
ota_err_t ota_boot_slot_set(uint8_t slot);

/**
 * @brief   Reports whether the running image still has to confirm itself.
 * @param   out_pending   receives true when this boot is the one trial boot a
 *                        freshly written image gets
 * @return  OTA_OK, OTA_ERR_PARAM when `out_pending` is NULL, OTA_ERR_IO when
 *          the boot state cannot be read.
 * @note    False on every ordinary boot, including one where rollback is not
 *          configured at all.
 */
ota_err_t ota_pending_verify_is(bool *out_pending);

/**
 * @brief   Confirms the running image, cancelling the pending rollback.
 * @return  OTA_OK, OTA_ERR_STATE when this boot was not a trial boot,
 *          OTA_ERR_IO when the SDK refused.
 * @note    Call this only after something has actually checked that the image
 *          works. Calling it unconditionally at start-up turns the rollback
 *          into a formality (R-VER-08).
 */
ota_err_t ota_mark_valid(void);

/**
 * @brief   Opens a write session on a slot, erasing what was there.
 * @param   slot       0 .. `OTA_SLOT_COUNT` - 1, and not the running slot
 * @param   img_size   exact image size in bytes, 1 or more
 * @param   out        receives the session; set to `OTA_SESSION_NONE` on
 *                     failure
 * @return  OTA_OK, OTA_ERR_PARAM on a bad argument, OTA_ERR_NOT_FOUND when the
 *          slot is absent, OTA_ERR_NO_SPACE when `img_size` exceeds the slot,
 *          OTA_ERR_STATE when the target is the running slot, OTA_ERR_IO when
 *          the erase failed.
 * @note    **This erases the slot**, so every argument a caller can check
 *          should be checked before calling. The session holds flash-write
 *          state until `ota_session_end()` or `ota_session_abort()` releases
 *          it; one leaked per retry is the defect this API exists to make
 *          visible.
 */
ota_err_t ota_session_begin(uint8_t slot, uint32_t img_size, ota_session_t *out);

/**
 * @brief   Appends bytes to an open session, in order.
 * @param   session   an open session
 * @param   data      bytes to write; consumed during the call, not retained
 * @param   len       number of bytes, 1 or more
 * @return  OTA_OK, OTA_ERR_PARAM on a bad argument, OTA_ERR_STATE when the
 *          session is not open, OTA_ERR_IO when the flash write failed.
 * @note    Sequential only — there is no seek. The session remembers where it
 *          is, which is why the caller does not pass an offset.
 */
ota_err_t ota_session_write(ota_session_t session, const void *data, size_t len);

/**
 * @brief   Closes a session and makes the slot bootable if the image checks out.
 * @param   session   an open session
 * @return  OTA_OK, OTA_ERR_PARAM when `session` is `OTA_SESSION_NONE`,
 *          OTA_ERR_STATE when the session is not open, OTA_ERR_IO when the
 *          image failed validation.
 * @note    **The session is released either way**, success or failure, so the
 *          caller must not abort it afterwards. Arms nothing: a finalised slot
 *          is bootable, not booted — that is `ota_boot_slot_set()`.
 */
ota_err_t ota_session_end(ota_session_t session);

/**
 * @brief   Throws a session away, leaving the slot unfinalised.
 * @param   session   an open session
 * @return  OTA_OK, OTA_ERR_PARAM when `session` is `OTA_SESSION_NONE`,
 *          OTA_ERR_STATE when the session is not open.
 * @note    An unfinalised slot is inert: nothing but `ota_session_end()` makes
 *          one bootable, so a discarded transfer cannot be booted by accident.
 */
ota_err_t ota_session_abort(ota_session_t session);

#ifdef __cplusplus
}
#endif

#endif /* OTA_H */

/*** end of file ***/
