---
title: Flash Layout and Partitions
category: data
order: 3
purpose: The 16 MB partition table, why the two app slots hold two different applications, the three small data partitions, and the constraints a change must respect.
status: active
updated: 2026-09-07
source: workspace/0xF001/partitions.csv, workspace/0xF001/sdkconfig.defaults
confidence: confirmed
keywords: partitions.csv, app_updater, app_firmware, cfg_factory, cfg_setting, coredump, ota_0, ota_1, otadata, nvs, phy_init, rollback, CONFIG_ESPTOOLPY_FLASHSIZE_16MB, CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH
---

# Flash Layout and Partitions

> Two app slots holding two different applications — a small updater and the product firmware — with no factory partition, because the bootloader never rolls back to one. Three small data partitions: two in what used to be alignment padding, and a 64 KB core dump at the top of flash.

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
| `app_firmware` | app | ota_1 | `0x220000` | `0xDD0000` | `0xFF0000` |
| `coredump` | data | coredump | `0xFF0000` | `0x10000` | `0x1000000` |

The first partition starts at `0xF000`, not at the earliest legal `0x9000`:
`0x9000 .. 0xF000` (24 KB) is deliberately left empty behind the partition
table. Nothing in the build claims it, so it is available for whatever it was
reserved for without moving anything else.

`app_updater` is 2 MB, `app_firmware` the 13.8125 MB between it and the core
dump. `0x1D000 .. 0x20000` (12 KB) is unused padding, spent to put the first
app slot on a 64 KB boundary. Nothing is free at the top of flash —
`coredump` runs to the last byte.

Generated and re-read with ESP-IDF v6.1's own `gen_esp32part.py` against
`--flash-size 16MB`, which accepts the table with no warning and reads back
`app_firmware` as `14144K` ending exactly at `0xFF0000`. Built end to end:
`idf.py build` emits `--flash-size 16MB` in its flash line, places
`ota_data_initial.bin` at `0x15000`, and `check_sizes.py` reports
`app_updater.bin` at `0x31100` bytes against a `0x200000` slot, 90% free.

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

**Both repos must ship the same table.** The two applications share one flash,
so `partitions.csv` here and in the sibling `app-firmware` repo have to be
byte-identical in effect: a unit is flashed with one table and then both images
must agree about where `otadata`, `cfg_*` and `coredump` live. The sibling repo
is empty today, so nothing needs syncing yet — the first commit there does.

## `cfg_factory`, `cfg_setting` and `coredump`

Three data partitions, none large enough to hold an image. The two `cfg_` ones
are named `cfg_` and not `app_` precisely so that no one reads them as a third
app slot.

| Name | Size | SubType | Why that subtype |
|------|------|---------|------------------|
| `cfg_factory` | 4 KB (1 sector) | `undefined` | NVS needs **three** sectors minimum, so 4 KB cannot be an NVS partition. Read and erased raw through `esp_partition_read` / `esp_partition_erase_range`. Intended for provisioning data written in production — serial number, calibration. |
| `cfg_setting` | 16 KB (4 sectors) | `undefined` | Runtime settings, read and erased raw like `cfg_factory`. Four sectors is also three plus one spare, so it *could* be handed to NVS later by changing the subtype alone — that is the only reason it is this size. |
| `coredump` | 64 KB (16 sectors) | `coredump` | The subtype the `espcoredump` component looks for by `esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP)`; the label is free-form, the subtype is not. Enabled by `CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y`. |

Both `cfg_` partitions sit after `phy_init`, inside the alignment padding
before the first app slot, so adding them cost no app-slot space. Nothing in
the firmware opens either yet
([interface/storage-api.md](../interface/storage-api.md) still uses the default
`nvs` partition only).

### Why the core dump is 64 KB, and why it came off `app_firmware`

On a panic, `espcoredump` writes an ELF file here — CPU registers, the panic
reason, and the TCB plus stack of the crashed task and of the other tasks in
priority order. It survives the reboot, and `idf.py coredump-info` reads it
back. It is the only record a field unit keeps of why it died, because nobody
is watching UART0 there.

**Where the 64 KB comes from.** 64 KB (`0x10000`) is Espressif's recommended
size for a core dump partition and is what every default ESP-IDF table uses.
The documented worst-case bound is
`20 + max_tasks × (12 + TCB size + max task stack size)` bytes — pessimistic,
because it assumes every one of `CONFIG_ESP_COREDUMP_MAX_TASKS_NUM` tasks is
present at its maximum stack. A real dump of this updater (one 4 KB `usb_cmd`
task plus the ESP-IDF system tasks) is a small fraction of 64 KB.

**What it is not enough for.** `CONFIG_ESP_COREDUMP_CAPTURE_DRAM` adds the
whole `.bss`, `.data` and heap to the dump and wants **at least 128 KB**. It is
off, and turning it on means growing this partition first.

**Taken off the tail of `app_firmware`, not from anywhere else.** Every offset
in the table is exactly where it was before `coredump` existed — `nvs`,
`otadata`, `phy_init`, both `cfg_` partitions and **both app slots** start
unchanged. Only `ota_1` got shorter, `0xDE0000 → 0xDD0000` (13.875 → 13.8125
MB), and it has megabytes to spare. The two padding gaps could not have been
used instead: `0x9000 .. 0xF000` is 24 KB and `0x1D000 .. 0x20000` is 12 KB,
neither reaches 64 KB and they are not contiguous.

**The failure mode if it is ever too small:** `espcoredump` logs `Not enough
space to save core dump!` and drops the dump. Nothing else breaks — the panic
still reboots the unit as before.

## Invariants

1. **No `factory` partition.** Rollback works only between `ota_x` slots;
   `cfg_factory` is a data partition and plays no part in boot selection.
2. App partitions must start on a 64 KB boundary. `0x20000` and `0x220000` both
   satisfy this; an arbitrary size change usually breaks it. Data partitions
   only need 4 KB (sector) alignment — `coredump` at `0xFF0000` satisfies both
   anyway.
3. Nothing may start before `0x9000` — the table occupies `0x8000` for `0xC00`
   bytes and takes a full 4 KB sector. Here the first partition starts later
   still, at `0x9000 + 0x6000 = 0xF000`, by design.
4. `otadata` must be exactly `0x2000` (two sectors): the bootloader alternates
   between them so a power cut never destroys the only good copy.
5. The two app slots are **not the same size**: ota_0 is 2 MB, ota_1 the
   13.8125 MB between it and `coredump`. The updater always fits either slot; a
   firmware larger than 2 MB fits only `app_firmware`, so it can never be
   staged into ota_0. Today's `app_updater.bin` is 198 KB, nowhere near either
   ceiling.
6. A partition label is at most 16 characters. All six of ours fit.
7. Neither `cfg_` partition is NVS today; both are raw. Any NVS partition needs
   at least three 4 KB sectors, so `cfg_setting` (4) could become one but
   `cfg_factory` (1) never can.
8. Exactly **one** partition may carry subtype `coredump` — `espcoredump` takes
   the first it finds and never looks for a second.
9. Changing any size means changing `CONFIG_ESPTOOLPY_FLASHSIZE_*` in
   `sdkconfig.defaults` **in the same commit**, and an OTA image built against
   the old table will not fit the new one — that is a MAJOR version bump. The
   table is flashed once per unit, so resizing `coredump` later is a field
   operation, not a config edit: pick the size before shipping.

**Caveat:** the table is arithmetic-checked, accepted by `gen_esp32part.py`
and built against, but has **never been flashed to a device**. In
particular nothing has confirmed the part fitted is really 16 MB — `bsp_init()`
now reads that from the chip, so the first boot log settles it. **Nothing has
ever written or read a core dump either**: the partition and the config are in
place, but only a real panic on a real board proves the write path, and only
`idf.py coredump-info` against that dump proves it is readable.

## See also

- [../architecture/build-and-toolchain.md](../architecture/build-and-toolchain.md) — the config that selects this table
- [../behavior/boot-and-bring-up.md](../behavior/boot-and-bring-up.md) — how the running slot confirms itself
