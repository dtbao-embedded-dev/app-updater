---
title: Known Deviations and Open Holes
category: rule
order: 7
purpose: Every place this repo departs from the house standard on purpose, plus the unfinished work that must not ship.
status: active
updated: 2026-09-07
source: application/app/src/app.c:176-193, application/updater/src/updater.c:200-215, middleware/storage/src/storage.c:182-207, driver/bsp/src/bsp.c:22-33, CHANGELOG.md, conversation
confidence: confirmed
keywords: SPEC-DEVIATION, TODO, R-VER-08, R-RPO-06, R-RPO-01, gap, self-test, migration, usb, console UART0, PROTOCOL_ERR_UNSUPPORTED, PING ceiling
---

# Known Deviations and Open Holes

> Nine deliberate departures and six unfinished holes; the holes are the list that must be empty before a field release.

## When this applies

Before a release, before running `spec-verify`, and whenever a reviewer asks why
this repo does not match the standard.

## Deliberate deviations

| # | Rule | What this repo does instead | Why |
|---|------|------------------------------|-----|
| 1 | `R-RPO-06` | Developer scripts live in `docs/scripts/`, not a repo-root `scripts/` | User decision. `spec-verify` will flag it. If it is to stay, change the standard rather than letting each repo drift. |
| 2 | `R-RPO-01` | A `<mod>_priv.h` exists only in `application/app/` | The private header is the home for declarations shared across split parts; no module is split yet. The one that exists carries the `app_main` prototype that `-Wmissing-prototypes` demands. |
| 3 | LOG doc prose | No `LOG_E`/`LOG_W`/`LOG_I` wrapper; the SDK log macros are used directly with the module `TAG` | A wrapper usable by `driver/bsp/` would have to sit below the driver layer, which is exactly where the SDK's own logging already sits. Every numbered LOG rule still holds. |
| 4 | `R-RPO-09` tree | No `third_party/`; `test/` holds only the host harness, no on-target test | Nothing to put in `third_party/` yet. The on-target smoke test is missing, which is a hole rather than a departure — see below. |
| 5 | naming | The project-wide status is `fw_err_t`, not the `updater_err_t` first proposed | `updater_err_t` would collide with the `updater` module's symbol prefix, and a grep for a prefix must land in exactly one place. `fw_err_t` is the standard's own name for the app-wide type. |
| 6 | `R-VER-13` | Once anything is released, the changelog splits one-file-per-version under `docs/CHANGELOG/`; the root `CHANGELOG.md` keeps `[Unreleased]` plus an index. In use since v0.1.0 | User decision. The standard wants one newest-first file, so a reader or tool looking for "what changed in 0.1.0" no longer finds it in the conventional place. The index table is what keeps the trail followable. |
| 7 | source USB spec | The console and boot log are on **UART0** (GPIO43/44), not on USB-Serial-JTAG as the source spec's product has them | Not a choice. ESP32-S3 has one internal USB PHY, time-division shared between USB-OTG and USB-Serial-JTAG (TRM 32.3.1, 33.3.1), and TinyUSB claims it during install. Keeping the log on USB-Serial-JTAG would silence it the moment the command channel came up. **Cost:** `tool-esp.py flash` loses its USB auto-download reset - flash over UART0, or hold BOOT. Never burn `EFUSE_USB_PHY_SEL` to "fix" this: it is one-way and removes USB-Serial-JTAG download in the bootloader too. |
| 8 | source USB spec | 36 of the 46 defined opcodes answer `PROTOCOL_ERR_UNSUPPORTED` (`-7`) - the whole ATE range, all of Config, every Wi-Fi and network item, and `FACTORY_RESET` | The spec was written for product 0x0001, which has an LCD, touch panel, SD card, Ethernet, Wi-Fi and a config registry. This board has none of them. `-7` is the spec's own answer for "the opcode is right, this build cannot serve it", and it is the honest one: a stub returning OK would ship a unit carrying a check that never checked anything. See [../interface/command-map.md](../interface/command-map.md). |
| 9 | source USB spec | `PING` refuses a payload above **32768** bytes with `-3`, not the `PROTOCOL_MAX_DATA` (32772) the spec implies | The spec contradicts itself: it says PING echoes up to `PROTOCOL_MAX_DATA` bytes **and** that the reply carries `4 + REQ.LENGTH`, which at the cap asks for a frame past that same cap. Refusing the length beats truncating the echo. `UPG_WRITE` is unaffected - its reply is a status only, which is exactly why the cap is 32772. |

## Open holes (must be empty before a field release)

| # | Where | Hole | Consequence if shipped |
|---|-------|------|------------------------|
| 1 | `app.c`, `confirm_or_roll_back()` | `SPEC-DEVIATION(R-VER-08)` — the new image confirms itself with **no self-test** | A broken image marks itself valid and rollback never fires. This defeats the product's whole reason to exist. **Shipped in v0.1.0**, listed in that release's notes. |
| 2 | `updater.c`, `step_checking()` | Not implemented — never finds an update | The updater never updates. Fails safe, but does nothing. |
| 3 | `updater.c`, `step_downloading()` | Not implemented | Unreachable today. |
| 4 | `storage.c`, `record_validate()` | No migration between record versions | Adding a field silently costs every deployed unit its stored settings. |
| 5b | v0.1.0 assets | Its published files cannot flash a blank board: `ota_data_initial.bin` missing, `flash_args` paths not flat | Anyone provisioning from that release has to read the offsets out of the notes by hand. Fixed for the next tag; v0.1.0 itself cannot be changed, because a tag never moves. |
| 5 | `bsp.c` board table | GPIO, polarity and flash size are placeholders, never checked against a schematic | The LED drives the wrong pin, or a pin that is wired to something else. |
| 6 | `partitions.csv` | Only ONE updater image exists — the two app slots hold different programs | A bad updater has no slot to roll back to. See [../data/flash-and-partitions.md](../data/flash-and-partitions.md). |

Two more, outside the source:

- Network bring-up (Wi-Fi or Ethernet) is not in this repo. The updater assumes
  something else brought the interface up before a fetch runs.
- There is **no on-target test**. An on-target smoke test that boots, exercises
  every peripheral once and prints a verdict is the last gate before a release
  tag (R-TST-10), and it does not exist. The host suite runs and is enforced in
  CI — see [testing.md](testing.md).
- Both GitHub Actions workflows have now run green, so this list is the only
  thing between the repo and a release that means something.

## The rule

1. A deliberate departure carries `SPEC-DEVIATION(R-XXX-nn)` in the source **and**
   a row in the table above. One without the other is a defect.
2. Unfinished work is `TODO(<name>): what and when`, never a bare `TODO`.
3. Nothing in the "open holes" table may reach a tagged release.

## See also

- [coding-standard-source.md](coding-standard-source.md) — how to resolve the rule IDs above
- [../behavior/boot-and-bring-up.md](../behavior/boot-and-bring-up.md) — hole 1 in context
