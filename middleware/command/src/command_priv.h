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

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------ Public function prototypes ------------------- */

/*
 * There is no slot-to-partition helper here any more. `BOOT_SLOT` and
 * `UPG_BEGIN`'s `target` carry the same numbering `driver/ota` indexes its
 * slots by, so the wire byte is handed straight to the driver and the mapping
 * lives in exactly one place - inside the driver, where the partition table
 * is.
 */

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

/*
 * The core dump range takes no session, so each of these three takes exactly
 * what it needs and nothing more - the same way the small handlers in
 * `command.c` do. There is no `command_dump_t` because there is no state: an
 * offset and a length arrive in every request, so a host may retry any chunk
 * in any order and a reboot forgets nothing worth remembering.
 */

/**
 * @brief   Reports what the core dump partition holds.
 * @param   payload       receives `[state:1][rsv:3][size:4]`, little-endian
 * @param   payload_len   set to `PROTOCOL_DUMP_INFO_RSP_LEN`
 * @return  `PROTOCOL_OK` — **including for an absent dump**, which is an
 *          answer and not a failure, the same doctrine as an unset version in
 *          `VERSION`; `PROTOCOL_ERR_HW` when the driver could not look.
 * @note    `state` is `coredump_state_t` unchanged: `0` absent, `1` valid, `2`
 *          stored but failing its checksum. Two encodings for one fact is a
 *          bug waiting to happen.
 */
protocol_status_t command_dump_info(uint8_t *payload, uint32_t *payload_len);

/**
 * @brief   Answers one chunk of the stored dump, staged in `cmd->chunk`.
 * @param   cmd           dispatcher instance, for its staging buffer
 * @param   req           a DUMP_READ whose LENGTH the map already checked
 * @param   payload_len   set to the number of bytes read
 * @param   echo          pointed at `cmd->chunk`, which the frame builder
 *                        copies once — the payload is far too big for the
 *                        dispatcher's 32-byte stack buffer
 * @return  `PROTOCOL_OK`; `PROTOCOL_ERR_BAD_ARG` for a `len` of zero, a `len`
 *          over `PROTOCOL_DUMP_CHUNK_MAX`, or a range reaching past the stored
 *          dump; `PROTOCOL_ERR_STATE` when no dump is stored at all;
 *          `PROTOCOL_ERR_HW` when the flash refused.
 * @note    `-4` and not `-3` for a bad `len`: the request LENGTH is a correct
 *          eight bytes, so what is wrong is a value inside it. A dump that
 *          fails its checksum still reads — that is exactly the one worth
 *          reading.
 */
protocol_status_t command_dump_read(command_t *cmd, const protocol_req_t *req,
                                    uint32_t *payload_len, const uint8_t **echo);

/**
 * @brief   Clears the stored dump so the next panic has room.
 * @return  `PROTOCOL_OK` — **including when there was nothing to erase**, so a
 *          host retrying after a lost reply cannot fail on the second try;
 *          `PROTOCOL_ERR_HW` when the erase failed.
 * @note    Load-bearing once `CONFIG_ESP_COREDUMP_FLASH_NO_OVERWRITE` is on: a
 *          unit whose dump is read but never erased captures no further panic.
 */
protocol_status_t command_dump_erase(void);

#ifdef __cplusplus
}
#endif

#endif /* COMMAND_PRIV_H */

/*** end of file ***/
