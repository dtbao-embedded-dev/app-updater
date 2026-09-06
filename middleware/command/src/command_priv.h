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

#ifdef __cplusplus
}
#endif

#endif /* COMMAND_PRIV_H */

/*** end of file ***/
