---
title: USB Command Map
category: interface
order: 10
purpose: Every opcode the protocol defines, which ten this build serves, and why each of the rest answers -7.
status: active
updated: 2026-09-07
source: middleware/protocol/src/protocol.c, middleware/protocol/include/protocol.h, middleware/command/src/command.c
confidence: confirmed
keywords: command map, opcode, PROTOCOL_CMD_SERVED, PROTOCOL_CMD_UNSUPPORTED, PROTOCOL_ERR_BAD_CMD, PROTOCOL_ERR_UNSUPPORTED, RESTART_APP, PING, BOOT_SLOT, VERSION, WIFI_MAC, BLE_MAC, UPG_BEGIN, UPG_WRITE, UPG_END, retired item
---

# USB Command Map

> 46 opcodes are defined; **ten** are served. The other 36 exist in the table
> only so they can answer `-7` instead of `-2`.

## Why the unserved rows exist

The source spec was written for product 0x0001 — an ESP32-P4 with an LCD, a
touch panel, an SD card, Ethernet, Wi-Fi and a config registry. **This board has
none of them.** The wire format, the parser rules and the opcode numbers are
adopted verbatim; only the opcodes this hardware can honestly answer get a
handler.

Keeping the rest in the map is what makes the distinction possible:

- an opcode **absent** from the map answers `PROTOCOL_ERR_BAD_CMD` (`-2`) — the
  spec does not define it, so fix the request;
- an opcode **present but not served** answers `PROTOCOL_ERR_UNSUPPORTED`
  (`-7`) — the request is right and this build cannot serve it, so the fix is a
  different build.

Without the unserved rows, a host asking for `CHK_LCD` would be told "no such
command", which is a lie.

A **retired** item stays absent on purpose. Get System `0x06` was `PRODUCT_ID`
and is gone; a tool built against the old map gets `-2` and learns the command
disappeared, rather than reaching whatever number took its place.

## Served (ten)

| COMMAND | Name | REQ.DATA | RSP payload after STATUS |
|---------|------|----------|--------------------------|
| `0x0001` | RESTART_APP | — | — reply first, wait 100 ms, then reset |
| `0x0006` | PING | 0..32768 B, any | the request's DATA, byte for byte |
| `0x0101` | Set BOOT_SLOT | `[slot:1]` | — arms the **next** boot |
| `0x0201` | VERSION | — | `[0:16]` running updater, `[16:32]` `app_firmware` slot |
| `0x0202` | Get BOOT_SLOT | — | `[slot:1]` the slot running **now** |
| `0x0203` | WIFI_MAC | — | `[mac:6]` from eFuse |
| `0x0204` | BLE_MAC | — | `[mac:6]` from eFuse |
| `0x0601` | UPG_BEGIN | `[target:1][img_size:4][img_crc32:4][chunk_max:4]` | — |
| `0x0602` | UPG_WRITE | `[offset:4][chunk]` | — |
| `0x0603` | UPG_END | — | — verifies and finalises; arms nothing |

`slot` and `target` share one encoding: `0` = `app_updater` (ota_0), `1` =
`app_firmware` (ota_1). Two encodings for one pair of slots is a bug waiting to
happen.

Both MACs come from eFuse, which is why they are served with no radio brought up
and no BLE stack linked — and why every ATE hardware check is not.

## Unsupported, by range, with the reason

| Range | Items | Why `-7` here |
|-------|-------|---------------|
| Action `0x00` | `02` FACTORY_RESET | Nothing in this repo owns a CFG region. `cfg_setting` exists as a raw partition with no reader or writer |
| Action `0x00` | `03` WIFI_CONNECT, `04` WIFI_SCAN, `05` NET_STATUS | No Wi-Fi driver in this repo at all; networking is not brought up here |
| Set/Get System | `0x0102`/`0x0205` GUID | No identity store. `cfg_factory` is an unowned raw partition |
| Set Config `0x03` | `01` MSC_ENABLE, `03` LANGUAGE, `04` BLE_NAME, `06` CUSTOMER_ID, `07` LCD_BRIGHTNESS, `08` LOG_LEVEL, `0A` NET_MODE | No config registry, and no SD card, display, BLE name or network mode to configure |
| Get Config `0x04` | the same rows plus `05` WIFI_STA, `09` LOG_TAGS, `FF` CFG_VER | Same. A **set** on a read-only row (`05`, `09`, `FF`) is absent from the map rather than unsupported, because it is not a command at all — so it answers `-2` |
| ATE `0x05` | `01`–`03` provisioning, `10`–`19` hardware checks | No identity store, and no LCD, touch panel, LED, BLE stack, Wi-Fi, Ethernet PHY, SD card or RTC on this board |

## Length before value

Each row carries the exact REQ `LENGTH` its command takes, or
`PROTOCOL_LEN_ANY` when only the handler can judge it. The width is checked
**before** any value is looked at, so a two-byte `BOOT_SLOT` payload is `-3`
whatever it contains while a one-byte payload carrying `2` is `-4`. A tool is
always told which half of its request to fix.

## Keeping this honest

`middleware/command/test/test_command.c` walks the whole 16-bit opcode space and
asserts that **exactly** the ten opcodes above report `PROTOCOL_CMD_SERVED`. An
opcode drifting into or out of the set fails there rather than on a bench.

## See also

- [../behavior/usb-command-dispatch.md](../behavior/usb-command-dispatch.md) — what each served handler does
- [../behavior/usb-upgrade-session.md](../behavior/usb-upgrade-session.md) — the `0x06` range in detail
- [protocol-api.md](protocol-api.md) — the lookup that returns these rows
- [../rule/known-deviations.md](../rule/known-deviations.md) — the ranges left at `-7`, as a deviation
