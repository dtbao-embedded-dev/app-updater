---
title: OTA Slot API
category: interface
purpose: The driver contract for the app slots - what each holds, which one runs, how an image is written into one, and how a fresh boot is confirmed.
status: active
updated: 2026-09-07
source: driver/ota/include/ota.h, driver/ota/src/ota.c
confidence: confirmed
keywords: ota.h, ota_err_t, ota_err_str, ota_session_t, OTA_SESSION_NONE, OTA_SLOT_COUNT, OTA_VERSION_MAX, ota_slot_size_get, ota_slot_version_get, ota_running_slot_get, ota_running_version_get, ota_boot_slot_set, ota_pending_verify_is, ota_mark_valid, ota_session_begin, ota_session_write, ota_session_end, ota_session_abort
---

# OTA Slot API

> The only module in this repo that names `esp_ota_*` or `esp_partition_*`; everything above it addresses slots by index and holds sessions as an opaque word.

## Responsibility

Owns the two app slots: their sizes, the version in each one's image header,
which one the running code booted from, which one boots next, and the write
session that puts a new image into one. It knows nothing about *why* a slot is
being written - that is the caller's business (R-LAY-06).

## Constants

| Name | Value | Meaning |
|------|-------|---------|
| `OTA_SLOT_COUNT` | `2U` | Slots this driver addresses, numbered from 0 in partition-table order. On this product slot 0 is `app_updater` and slot 1 is `app_firmware`, but the driver is told an index and knows nothing about what either is for. |
| `OTA_VERSION_MAX` | `32U` | Capacity a version buffer needs, NUL included - the widest version field an image header carries, so a caller passing this much never sees a truncated answer. |
| `OTA_SESSION_NONE` | `0U` | An `ota_session_t` naming no session. |

The wire protocol numbers its slots the same way (`PROTOCOL_SLOT_UPDATER` = 0,
`PROTOCOL_SLOT_FIRMWARE` = 1), so `middleware/command` hands the byte off the
wire straight to this driver. Two encodings for one pair of slots is a bug
waiting to happen; there is one.

## Types

| Type | Shape | Why |
|------|-------|-----|
| `ota_err_t` | `OTA_OK` 0, `OTA_ERR_PARAM` -1, `OTA_ERR_STATE` -2, `OTA_ERR_NO_SPACE` -4, `OTA_ERR_NOT_FOUND` -6, `OTA_ERR_IO` -7 | A driver may not include `fw.h` (R-LAY-01), so it carries its own space; the generic `-1 .. -19` values mean what they mean everywhere else (R-ERR-03). |
| `ota_session_t` | `uint32_t` | A vendor handle behind a plain integer: `esp_ota_handle_t` may not appear above the driver layer (R-LAY-03), and a caller has nothing to do with the value but hand it back. |

## Signatures

```c
const char *ota_err_str(ota_err_t err);

ota_err_t ota_slot_size_get(uint8_t slot, uint32_t *out_size);
ota_err_t ota_slot_version_get(uint8_t slot, char *out, size_t cap);
ota_err_t ota_running_slot_get(uint8_t *out_slot);
ota_err_t ota_running_version_get(char *out, size_t cap);
ota_err_t ota_boot_slot_set(uint8_t slot);

ota_err_t ota_pending_verify_is(bool *out_pending);
ota_err_t ota_mark_valid(void);

ota_err_t ota_session_begin(uint8_t slot, uint32_t img_size, ota_session_t *out);
ota_err_t ota_session_write(ota_session_t session, const void *data, size_t len);
ota_err_t ota_session_end(ota_session_t session);
ota_err_t ota_session_abort(ota_session_t session);
```

There is no `ota_init()` / `ota_deinit()`: nothing here holds state between
calls except the session, which the caller holds. A lifecycle pair for a
stateless module would be two functions that do nothing.

## Contracts worth knowing before calling

| Call | The part a caller gets wrong |
|------|------------------------------|
| `ota_session_begin` | **It erases the slot.** Every argument a caller can check should be checked first. It refuses the running slot itself as well - the two-slot layout exists to prevent exactly that - but a caller must not rely on that being the only guard. |
| `ota_session_write` | Sequential only, no seek: the session remembers where it is, which is why there is no offset parameter. |
| `ota_session_end` | **Releases the session either way**, success or failure, so the caller must not abort it afterwards. Makes the slot bootable, not booted - arming is `ota_boot_slot_set()`. |
| `ota_session_abort` | Leaves the slot unfinalised, which is inert: nothing but `ota_session_end()` makes a slot bootable, so a discarded transfer cannot be booted by accident. |
| `ota_slot_version_get` | A slot never written answers `OTA_ERR_NOT_FOUND` and leaves `out` untouched, rather than returning an empty string. "Never written" and "written with an empty version" are different facts. |
| `ota_mark_valid` | Call it only after something has actually checked the image. Calling it unconditionally at start-up turns the rollback into a formality (R-VER-08) - which is exactly the open hole recorded in [../rule/known-deviations.md](../rule/known-deviations.md). |

## Where the vendor status stops

`from_esp_err()` in `driver/ota/src/ota.c` is the last place an `esp_err_t`
exists. It is **silent**, unlike the equivalent in `bsp`, `storage` and
`ota_http`: every caller inside this module already logs the raw vendor value
with context the mapper does not have - which slot, how many bytes - so logging
in both places would double every failure line.

## Host testing

`test/host/fake/ota_fake.c` implements this contract in RAM, with a control
surface (`ota_fake_reset`, `ota_fake_set_slot_size`, `ota_fake_fail_begin`,
`ota_fake_open_sessions`, `ota_fake_crc`, ...) that lets a test of
`middleware/command` drive the same API the target implementation satisfies.
That is the point of the driver existing: before it, the same tests had to
shadow `esp_ota_ops.h` and pretend to be ESP-IDF. See
[../rule/testing.md](../rule/testing.md).

## See also

- [../rule/layer-boundaries.md](../rule/layer-boundaries.md) — the rule this driver exists to satisfy
- [../behavior/usb-upgrade-session.md](../behavior/usb-upgrade-session.md) — the caller that drives a session end to end
- [../data/flash-and-partitions.md](../data/flash-and-partitions.md) — what the two slots hold and how big they are
