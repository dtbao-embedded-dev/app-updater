---
title: Flash Layout and Partitions
category: data
order: 3
purpose: The 4 MB partition table, the two-OTA-slot scheme, and the constraints a change to it must respect.
status: active
updated: 2026-09-06
source: workspace/0xF001/partitions.csv, workspace/0xF001/sdkconfig.defaults
confidence: confirmed
keywords: partitions.csv, ota_0, ota_1, otadata, nvs, phy_init, rollback, CONFIG_ESPTOOLPY_FLASHSIZE_4MB
---

# Flash Layout and Partitions

> Two OTA slots and no factory partition, because the bootloader never rolls back to a factory image.

## Shape

Product 0xF001, 4 MB (`0x400000`) SPI flash.

| Name | Type | SubType | Offset | Size | Ends at |
|------|------|---------|--------|------|---------|
| *(partition table)* | — | — | `0x8000` | `0xC00` | `0x8C00` |
| `nvs` | data | nvs | `0x9000` | `0x6000` | `0xF000` |
| `otadata` | data | ota | `0xF000` | `0x2000` | `0x11000` |
| `phy_init` | data | phy | `0x11000` | `0x1000` | `0x12000` |
| `ota_0` | app | ota_0 | `0x20000` | `0x1E0000` | `0x200000` |
| `ota_1` | app | ota_1 | `0x200000` | `0x1E0000` | `0x3E0000` |

Each app slot is 1.875 MB. `0x12000 .. 0x20000` (56 KB) is deliberately unused
padding, spent to put `ota_0` on a 64 KB boundary.

## Invariants

1. **No `factory` partition.** Rollback is only possible between `ota_x` slots;
   a factory image is never rolled back. Adding one would silently weaken the
   guarantee the whole product exists to provide.
2. App partitions must start on a 64 KB boundary. `0x20000` and `0x200000` both
   satisfy this; an arbitrary size change usually breaks it.
3. Nothing may start before `0x9000` — the table itself occupies `0x8000` for
   `0xC00` bytes and takes a full 4 KB sector.
4. `otadata` must be exactly `0x2000` (two sectors): the bootloader alternates
   between them so a power cut never destroys the only good copy.
5. The two app slots must be the **same size**. An image built for the larger
   one cannot be written into the smaller.
6. Changing any size means changing `CONFIG_ESPTOOLPY_FLASHSIZE_*` in
   `sdkconfig.defaults` **in the same commit**, and an OTA image built against
   the old table will not fit the new one — that is a MAJOR version bump.

## Reproduction notes

The whole table fits within `0x400000`, leaving `0x3E0000 .. 0x400000` (128 KB)
free at the top. That headroom is the natural home for a future SPIFFS/FAT or a
second NVS partition; taking it does not disturb the app slots.

**Caveat:** these offsets are read straight from the CSV and arithmetic-checked,
but have **never been flashed to a device**. The first successful `flash` is the
confirmation that the layout boots.

## See also

- [../architecture/build-and-toolchain.md](../architecture/build-and-toolchain.md) — the config that selects this table
- [../behavior/boot-and-bring-up.md](../behavior/boot-and-bring-up.md) — how the running slot confirms itself
