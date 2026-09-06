---
title: Flash Layout and Partitions
category: data
order: 3
purpose: The 16 MB partition table, why the two app slots hold two different applications, the two small data partitions, and the constraints a change must respect.
status: active
updated: 2026-09-06
source: workspace/0xF001/partitions.csv, workspace/0xF001/sdkconfig.defaults
confidence: confirmed
keywords: partitions.csv, app_updater, app_firmware, cfg_factory, cfg_setting, ota_0, ota_1, otadata, nvs, phy_init, rollback, CONFIG_ESPTOOLPY_FLASHSIZE_16MB
---

# Flash Layout and Partitions

> Two app slots holding two different applications — a small updater and the product firmware — with no factory partition, because the bootloader never rolls back to one. Two small data partitions sit in what used to be alignment padding.

## Shape

Product 0xF001, 16 MB (`0x1000000`) SPI flash.

| Name | Type | SubType | Offset | Size | Ends at |
|------|------|---------|--------|------|---------|
| *(partition table)* | — | — | `0x8000` | `0xC00` | `0x8C00` |
| *(reserved, empty)* | — | — | `0x9000` | `0x6000` | `0xF000` |
| `nvs` | data | nvs | `0xF000` | `0x6000` | `0x15000` |
| `otadata` | data | ota | `0x15000` | `0x2000` | `0x17000` |
| `phy_init` | data | phy | `0x17000` | `0x1000` | `0x18000` |
| `cfg_factory` | data | undefined | `0x18000` | `0x1000` | `0x19000` |
| `cfg_setting` | data | undefined | `0x19000` | `0x4000` | `0x1D000` |
| `app_updater` | app | ota_0 | `0x20000` | `0x200000` | `0x220000` |
| `app_firmware` | app | ota_1 | `0x220000` | `0xDE0000` | `0x1000000` |

The first partition starts at `0xF000`, not at the earliest legal `0x9000`:
`0x9000 .. 0xF000` (24 KB) is deliberately left empty behind the partition
table. Nothing in the build claims it, so it is available for whatever it was
reserved for without moving anything else.

`app_updater` is 2 MB, `app_firmware` the 13.875 MB that is left. `0x1D000 ..
0x20000` (12 KB) is unused padding, spent to put the first app slot on a 64 KB
boundary. Nothing is free at the top of flash any more — `app_firmware` runs to
the last byte.

Generated and re-read with ESP-IDF v6.1's own `gen_esp32part.py` against
`--flash-size 16MB`, and built end to end: `idf.py build` emits
`--flash-size 16MB` in its flash line, places `ota_data_initial.bin` at
`0x15000`, and `check_sizes.py` reports `app_updater.bin` at `0x31100` bytes
against a `0x200000` slot, 90% free.

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

## `cfg_factory` and `cfg_setting`

Two data partitions, neither large enough to hold an image and neither 64 KB
aligned. They are named `cfg_` and not `app_` precisely so that no one reads
them as a third app slot.

| Name | Size | SubType | Why that subtype |
|------|------|---------|------------------|
| `cfg_factory` | 4 KB (1 sector) | `undefined` | NVS needs **three** sectors minimum, so 4 KB cannot be an NVS partition. Read and erased raw through `esp_partition_read` / `esp_partition_erase_range`. Intended for provisioning data written in production — serial number, calibration. |
| `cfg_setting` | 16 KB (4 sectors) | `undefined` | Runtime settings, read and erased raw like `cfg_factory`. Four sectors is also three plus one spare, so it *could* be handed to NVS later by changing the subtype alone — that is the only reason it is this size. |

Both sit after `phy_init`, inside the alignment padding before the first app
slot, so adding them cost no app-slot space. Nothing in the firmware opens
either yet
([interface/storage-api.md](../interface/storage-api.md) still uses the default
`nvs` partition only).

## Invariants

1. **No `factory` partition.** Rollback works only between `ota_x` slots;
   `cfg_factory` is a data partition and plays no part in boot selection.
2. App partitions must start on a 64 KB boundary. `0x20000` and `0x220000` both
   satisfy this; an arbitrary size change usually breaks it. Data partitions
   only need 4 KB (sector) alignment.
3. Nothing may start before `0x9000` — the table occupies `0x8000` for `0xC00`
   bytes and takes a full 4 KB sector. Here the first partition starts later
   still, at `0x9000 + 0x6000 = 0xF000`, by design.
4. `otadata` must be exactly `0x2000` (two sectors): the bootloader alternates
   between them so a power cut never destroys the only good copy.
5. The two app slots are **not the same size**: ota_0 is 2 MB, ota_1 is the
   13.875 MB remainder. The updater always fits either slot; a firmware larger
   than 2 MB fits only `app_firmware`, so it can never be staged into ota_0.
   Today's `app_updater.bin` is 198 KB, nowhere near either ceiling.
6. A partition label is at most 16 characters. All five of ours fit.
7. Neither `cfg_` partition is NVS today; both are raw. Any NVS partition needs
   at least three 4 KB sectors, so `cfg_setting` (4) could become one but
   `cfg_factory` (1) never can.
8. Changing any size means changing `CONFIG_ESPTOOLPY_FLASHSIZE_*` in
   `sdkconfig.defaults` **in the same commit**, and an OTA image built against
   the old table will not fit the new one — that is a MAJOR version bump.

**Caveat:** the table is arithmetic-checked, accepted by `gen_esp32part.py`
and built against, but has **never been flashed to a device**. In
particular nothing has confirmed the part fitted is really 16 MB — `bsp_init()`
now reads that from the chip, so the first boot log settles it.

## See also

- [../architecture/build-and-toolchain.md](../architecture/build-and-toolchain.md) — the config that selects this table
- [../behavior/boot-and-bring-up.md](../behavior/boot-and-bring-up.md) — how the running slot confirms itself
