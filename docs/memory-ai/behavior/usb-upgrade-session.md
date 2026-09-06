---
title: USB Upgrade Session
category: behavior
order: 7
purpose: The chunked image transfer over USB - what is checked before anything is erased, the chunk rules, and how a session ends.
status: active
updated: 2026-09-07
source: middleware/command/src/command_upgrade.c, middleware/command/include/command.h, middleware/command/test/test_command.c
confidence: confirmed
keywords: command_upgrade_begin, command_upgrade_write, command_upgrade_end, command_upgrade_t, chunk_max, img_crc32, esp_ota_begin, esp_ota_write, esp_ota_end, esp_ota_abort, no abort opcode
---

# USB Upgrade Session

> `UPG_BEGIN` → N × `UPG_WRITE` → `UPG_END`. Nothing in that sequence can make
> a slot bootable except the last step, which is what makes every earlier step
> safe to abandon.

## The session

`command_upgrade_t`, carried inside `command_t`:

| Field | Holds |
|-------|-------|
| `is_open` | a transfer is in progress |
| `target` | the slot being written, `PROTOCOL_SLOT_*` |
| `img_size` | total image bytes the host declared |
| `img_crc32` | the CRC-32 the finished image must match |
| `chunk_max` | the accepted chunk size, enforced on every write |
| `written` | bytes written, **and the next offset expected** |
| `crc` | running CRC-32 over what has been written |
| `ota_handle` | the vendor handle, kept as a plain word so no SDK type reaches a public header (R-LAY-03) — the same trick `storage_t` uses |

## UPG_BEGIN: everything checked before anything is erased

In this order, and all of it before `esp_ota_begin()`:

| Check | Failure |
|-------|---------|
| `target` is 0 or 1 | `-4` |
| the slot exists in this build's partition table | `-4` |
| `target` is **not** the running slot | `-5` |
| the update cycle is not already writing (`is_busy`) | `-5` |
| `img_size` is non-zero and fits the partition | `-4` |
| `chunk_max` is a multiple of 1024 | `-4` |
| `chunk_max` is within 4096..32768 | `-4` |

Only then is the previous session discarded and the new one opened. A rejected
`UPG_BEGIN` therefore **erases nothing and closes nothing** — the host loses a
round trip and no more, and learns its request is wrong immediately instead of
after minutes of writing.

Refusing the running slot is the two-slot layout doing its job: overwriting the
image underneath the running code is exactly what it exists to prevent.

Zero is refused as a size because there is no such image, and it would make the
first chunk also the last.

## There is no abort opcode

`UPG_BEGIN` **always starts over.** Sent while a transfer is open it discards
that session and begins a new one; it never answers "busy with another
transfer". A host that gave up half way, picked the wrong file or lost the cable
simply sends it again, and one that crashed mid-transfer needs no recovery step.

That is only safe because the old handle goes **first**, before the new one is
opened. Otherwise a host retrying repeatedly strands one OTA handle per attempt
— the leak the source spec names explicitly. Two host tests assert the open
session count returns to one, and dropping the free turns exactly those two red.

An abandoned session leaves a partly written slot, which is inert: nothing but
`UPG_END` marks a slot valid, so the bootloader has no reason to look at it.

## UPG_WRITE: strictly sequential

`[offset:4][chunk]`, with `LENGTH` free-form so the handler checks its own
widths:

| Rule | Failure |
|------|---------|
| `LENGTH` at least 4, for the offset | `-3` |
| a session is open | `-5` |
| `offset` equals `written` | `-5` |
| the chunk is not empty | `-3` |
| the chunk is within `chunk_max` **and** within 32768 | `-3` |
| the chunk does not run past `img_size` | `-3` |
| the chunk is a multiple of 1024 **unless it is the last** | `-3` |

The offset is on the wire so a host can prove it has not lost its place, **not
so it can seek**: re-sending a chunk that already landed is as much out of
order as skipping one.

**The last chunk is exempt from the 1024 rule** and carries the remainder. An
image size is not a multiple of 1024, and demanding one would mean padding every
image and teaching `UPG_END` to ignore the padding. The device knows which chunk
is last from `img_size`, so nothing says so on the wire.

The CRC is folded as the bytes go by, so `UPG_END` needs no second pass over
13 MB of flash.

A failed `esp_ota_write()` is `-6` and **advances nothing**, so the host may
retry the same chunk at the same offset.

## UPG_END: verify, finalise, arm nothing

| Condition | Result |
|-----------|--------|
| no session open | `-5` |
| `written` short of `img_size` | `-5`, and **the session stays open** — the transfer is simply unfinished, so the host carries on rather than starting over |
| the running CRC does not match `img_crc32` | `-6`, the session is discarded, and the slot is **not** finalised |
| `esp_ota_end()` refused | `-6`; the vendor call frees the session either way, so it is not aborted twice |
| otherwise | `PROTOCOL_OK` |

A CRC mismatch is `-6` rather than `-4` because the device cannot tell a wrong
`img_crc32` from a corrupted transfer, and corruption is overwhelmingly the
likelier of the two. Either way the slot must not be finalised.

**`UPG_END` deliberately does not arm and does not reboot.** The host follows
with `Set BOOT_SLOT` and `RESTART_APP`. That separation is what keeps a
half-written slot from ever being bootable — see
[usb-host-flow.md](usb-host-flow.md).

## The other writer

The scheduled HTTP update cycle writes the same `app_firmware` slot. USB
**yields** to it: `UPG_BEGIN` answers `-5` while `updater_state_get()` reports
`CHECKING` or `DOWNLOADING`. `-5` is retryable, which is the honest answer — the
download finishes and the next `UPG_BEGIN` is accepted.

The application's busy callback reads an **unreadable** updater state as busy:
refusing a transfer is recoverable, two writers on one slot is not.

## See also

- [usb-command-dispatch.md](usb-command-dispatch.md) — how a frame reaches here
- [usb-host-flow.md](usb-host-flow.md) — the sequence and its time budget
- [update-cycle-fsm.md](update-cycle-fsm.md) — the other writer
- [../data/flash-and-partitions.md](../data/flash-and-partitions.md) — the two slots
