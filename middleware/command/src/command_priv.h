/**
 * @file    command_priv.h
 * @date    2026-09-06
 * @brief   Declarations internal to the command module; never installed.
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

#ifndef COMMAND_PRIV_H
#define COMMAND_PRIV_H

/* ------------------------------ Includes ------------------------------- */

#include "command.h"
#include "protocol.h"

#include "esp_partition.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------ Public function prototypes ------------------- */

/**
 * @brief   Resolves a wire slot number to the partition it names.
 * @param   slot   `PROTOCOL_SLOT_UPDATER` (0) or `PROTOCOL_SLOT_FIRMWARE` (1)
 * @return  The partition, or NULL when this build has no such slot.
 * @note    Shared across the module's sources rather than duplicated, because
 *          `BOOT_SLOT` and `UPG_BEGIN`'s `target` carry the same encoding and
 *          two copies of that mapping is one that drifts. Any value other than
 *          1 resolves to the updater slot, so callers validate the byte first.
 *          Not installed: the encoding is the module's business, not its
 *          callers'.
 */
const esp_partition_t *command_slot_partition(uint8_t slot);

/**
 * @brief   Opens a transfer, discarding whichever one was already open.
 * @param   cmd   dispatcher instance carrying the session
 * @param   req   an UPG_BEGIN whose LENGTH the map already checked
 * @return  `PROTOCOL_OK`; `PROTOCOL_ERR_BAD_ARG` for an unknown target, an
 *          image that does not fit its slot, or a `chunk_max` outside the
 *          band; `PROTOCOL_ERR_STATE` when the target is the running slot or
 *          the update cycle is already writing; `PROTOCOL_ERR_HW` when the
 *          erase failed.
 * @note    Every argument is checked BEFORE the slot is erased, so a host
 *          learns its request is wrong immediately rather than after minutes
 *          of writing. It never reports "busy with another transfer": a host
 *          that gave up half way just sends UPG_BEGIN again.
 */
protocol_status_t command_upgrade_begin(command_t *cmd, const protocol_req_t *req);

/**
 * @brief   Appends one chunk at the offset the session expects.
 * @return  `PROTOCOL_OK`; `PROTOCOL_ERR_BAD_LEN` for a chunk that is empty,
 *          over `chunk_max` or the protocol ceiling, not a multiple of 1024
 *          when it is not the last, or that would run past `img_size`;
 *          `PROTOCOL_ERR_STATE` with no session open or at the wrong offset;
 *          `PROTOCOL_ERR_HW` when the flash write failed.
 */
protocol_status_t command_upgrade_write(command_t *cmd, const protocol_req_t *req);

/**
 * @brief   Verifies the whole image and finalises the slot. Arms nothing.
 * @return  `PROTOCOL_OK`; `PROTOCOL_ERR_STATE` with no session open or a
 *          transfer short of `img_size`; `PROTOCOL_ERR_HW` when the image CRC
 *          does not match or the slot refused to validate.
 * @note    Deliberately does not arm or reboot: the host follows with Set
 *          BOOT_SLOT and RESTART_APP, which is what keeps a half-written slot
 *          from ever being bootable.
 */
protocol_status_t command_upgrade_end(command_t *cmd);

#ifdef __cplusplus
}
#endif

#endif /* COMMAND_PRIV_H */

/*** end of file ***/
