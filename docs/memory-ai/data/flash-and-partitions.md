---
title: Flash Layout and Partitions
category: data
order: 3
purpose: The 4 MB partition table, why the two app slots hold two different applications, and the constraints a change must respect.
status: active
updated: 2026-09-06
source: workspace/0xF001/partitions.csv, workspace/0xF001/sdkconfig.defaults
confidence: confirmed
keywords: partitions.csv, app_updater, app_firmware, ota_0, ota_1, otadata, nvs, phy_init, rollback, CONFIG_ESPTOOLPY_FLASHSIZE_4MB
---

# Flash Layout and Partitions

> Two app slots holding two different applications — a small updater and the product firmware — with no factory partition, because the bootloader never rolls back to one.

## Shape

Product 0xF001, 4 MB (`0x400000`) SPI flash.

| Name | Type | SubType | Offset | Size | Ends at |
|------|------|---------|--------|------|---------|
| *(partition table)* | — | — | `0x8000` | `0xC00` | `0x8C00` |
| `nvs` | data | nvs | `0x9000` | `0x6000` | `0xF000` |
| `otadata` | data | ota | `0xF000` | `0x2000` | `0x11000` |
| `phy_init` | data | phy | `0x11000` | `0x1000` | `0x12000` |
| `app_updater` | app | ota_0 | `0x20000` | `0x1E0000` | `0x200000` |
| `app_firmware` | app | ota_1 | `0x200000` | `0x1E0000` | `0x3E0000` |

Each app slot is 1.875 MB. `0x12000 .. 0x20000` (56 KB) is deliberately unused
padding, spent to put the first app slot on a 64 KB boundary. `0x3E0000 ..
0x400000` (128 KB) is free at the top.

## The two slots are two different programs

This is the single most important thing about this table, and it is not the
ESP-IDF default.

| Slot | Holds | Repo |
|------|-------|------|
| `app_updater` (ota_0) | Small recovery app: fetch an image, write it to the other slot, mark it bootable | this repo |
| `app_firmware` (ota_1) | The product itself | sibling `app-firmware` repo |

The bootloader still selects a slot through `otadata`, so a firmware that fails
to confirm itself falls back to the updater, which can fetch a replacement.

**What it costs.** In the ordinary A/B arrangement, either slot can recover the
other. Here there is exactly **one** updater image, so the updater has nothing
to roll back to: a bad updater is a bad unit, recoverable only over the wire it
may no longer be able to open. Two ways to buy that back if it ever matters —
a third app slot on a larger flash, or a `factory` partition holding a
known-good updater (which is never rolled back but is also never updated).

## Invariants

1. **No `factory` partition.** Rollback works only between `ota_x` slots.
2. App partitions must start on a 64 KB boundary. `0x20000` and `0x200000` both
   satisfy this; an arbitrary size change usually breaks it.
3. Nothing may start before `0x9000` — the table occupies `0x8000` for `0xC00`
   bytes and takes a full 4 KB sector.
4. `otadata` must be exactly `0x2000` (two sectors): the bootloader alternates
   between them so a power cut never destroys the only good copy.
5. The two app slots must be the **same size** while either can be written into
   the other's place. If the updater is ever shrunk to buy the firmware room,
   that symmetry — and the fallback with it — is gone.
6. A partition label is at most 16 characters. `app_updater` (11) and
   `app_firmware` (12) both fit.
7. Changing any size means changing `CONFIG_ESPTOOLPY_FLASHSIZE_*` in
   `sdkconfig.defaults` **in the same commit**, and an OTA image built against
   the old table will not fit the new one — that is a MAJOR version bump.

**Caveat:** these offsets are read straight from the CSV and arithmetic-checked
(no overlap, both app slots 64 KB aligned, total `0x3E0000` inside 4 MB), but
have **never been flashed to a device**.

## See also

- [../architecture/build-and-toolchain.md](../architecture/build-and-toolchain.md) — the config that selects this table
- [../behavior/boot-and-bring-up.md](../behavior/boot-and-bring-up.md) — how the running slot confirms itself
