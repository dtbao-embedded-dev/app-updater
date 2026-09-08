---
title: Known Deviations and Open Holes
category: rule
order: 7
purpose: Every place this repo departs from the house standard on purpose, plus the unfinished work that must not ship.
status: active
updated: 2026-09-08
source: application/app/src/app.c:176-193, application/updater/src/updater.c:200-215, driver/storage/src/storage.c, driver/bsp/src/bsp.c:22-33, CHANGELOG.md, conversation
confidence: confirmed
keywords: SPEC-DEVIATION, TODO, R-VER-08, R-RPO-06, R-RPO-01, R-LAY-01, R-LAY-04, gap, self-test, migration, usb, console UART0, PROTOCOL_ERR_UNSUPPORTED, PING ceiling, esp_log exception, esp_http_client exception, mapped driver
---

# Known Deviations and Open Holes

> Twelve deliberate departures and six unfinished holes; the holes are the list that must be empty before a field release.

## When this applies

Before a release, before running `spec-verify`, and whenever a reviewer asks why
this repo does not match the standard.

## Deliberate deviations

| # | Rule | What this repo does instead | Why |
|---|------|------------------------------|-----|
| 1 | `R-RPO-06` | Developer scripts live in `docs/scripts/`, not a repo-root `scripts/` | User decision. `spec-verify` will flag it. If it is to stay, change the standard rather than letting each repo drift. |
| 2 | `R-RPO-01` | A `<mod>_priv.h` exists only in `application/app/` | The private header is the home for declarations shared across split parts; no module is split yet. The one that exists carries the `app_main` prototype that `-Wmissing-prototypes` demands. |
| 3 | LOG doc prose | No `LOG_E`/`LOG_W`/`LOG_I` wrapper; the SDK log macros are used directly with the module `TAG` | A wrapper usable by `driver/bsp/` would have to sit below the driver layer, which is exactly where the SDK's own logging already sits. Every numbered LOG rule still holds. This is also **exception 1** to this repo's middleware-calls-only-drivers rule - see [layer-boundaries.md](layer-boundaries.md). |
| 4 | `R-RPO-09` tree | No `third_party/`; `test/` holds only the host harness, no on-target test | Nothing to put in `third_party/` yet. The on-target smoke test is missing, which is a hole rather than a departure — see below. |
| 5 | naming | The project-wide status is `fw_err_t`, not the `updater_err_t` first proposed | `updater_err_t` would collide with the `updater` module's symbol prefix, and a grep for a prefix must land in exactly one place. `fw_err_t` is the standard's own name for the app-wide type. |
| 6 | `R-VER-13` | Once anything is released, the changelog splits one-file-per-version under `docs/CHANGELOG/`; the root `CHANGELOG.md` keeps `[Unreleased]` plus an index. In use since v0.1.0 | User decision. The standard wants one newest-first file, so a reader or tool looking for "what changed in 0.1.0" no longer finds it in the conventional place. The index table is what keeps the trail followable. |
| 7 | source USB spec | The console and boot log are on **UART0** (GPIO43/44), not on USB-Serial-JTAG as the source spec's product has them | Not a choice. ESP32-S3 has one internal USB PHY, time-division shared between USB-OTG and USB-Serial-JTAG (TRM 32.3.1, 33.3.1), and TinyUSB claims it during install. Keeping the log on USB-Serial-JTAG would silence it the moment the command channel came up. **Cost:** `tool-esp.py flash` loses its USB auto-download reset - flash over UART0, or hold BOOT. Never burn `EFUSE_USB_PHY_SEL` to "fix" this: it is one-way and removes USB-Serial-JTAG download in the bootloader too. |
| 8 | source USB spec | 36 of the 46 defined opcodes answer `PROTOCOL_ERR_UNSUPPORTED` (`-7`) - the whole ATE range, all of Config, every Wi-Fi and network item, and `FACTORY_RESET` | The spec was written for product 0x0001, which has an LCD, touch panel, SD card, Ethernet, Wi-Fi and a config registry. This board has none of them. `-7` is the spec's own answer for "the opcode is right, this build cannot serve it", and it is the honest one: a stub returning OK would ship a unit carrying a check that never checked anything. See [../interface/command-map.md](../interface/command-map.md). |
| 9 | source USB spec | `PING` refuses a payload above **32768** bytes with `-3`, not the `PROTOCOL_MAX_DATA` (32772) the spec implies | The spec contradicts itself: it says PING echoes up to `PROTOCOL_MAX_DATA` bytes **and** that the reply carries `4 + REQ.LENGTH`, which at the cap asks for a frame past that same cap. Refusing the length beats truncating the echo. `UPG_WRITE` is unaffected - its reply is a status only, which is exactly why the cap is 32772. |
| 10 | own rule, `R-LAY-01` read strictly | `middleware/ota_http` keeps `esp_http_client` and `esp-tls` in `PRIV_REQUIRES` - the one middleware component with an SDK dependency | **Exception 2** to [layer-boundaries.md](layer-boundaries.md). HTTP is a protocol stack, which the house layer table assigns to middleware; `driver/` is for "one device or peripheral role" and an HTTP client is not a device. Wrapping it would put a non-device in the driver layer to satisfy the letter of a rule whose purpose - the portability line at the driver/BSP seam (R-LAY-04) - it does not serve, since `esp_http_client` is portable across every ESP part this repo will build for. The vendor status still stops at the module boundary: `ota_http.c` has its own `from_esp_err()` and `ota_http_t` holds its handle as `void *`. |
| 11 | `R-LAY-01` table | This repo forbids what the standard's own table permits: middleware may **not** call "HAL primitives" directly, only mapped drivers | Stricter than the standard, not looser, so `spec-verify` will not flag it - but it is a departure and belongs here. The looser reading is what produced the defect: `command_upgrade_begin()` called `esp_ota_begin()`, which nailed the USB upgrade path to one vendor and left its tests impersonating ESP-IDF. Written up with both greps in [layer-boundaries.md](layer-boundaries.md). |

| 12 | own rule, `versioning-and-release.md` step 2 | The partition table changed and this ships as **PATCH `0.1.1`**, not MAJOR | User decision, and the release line explains it: the branch is `release/v0.1`, so 0.1.x is its own series and 1.0.0 would come from a `release/v1.0`. The cost is real and does not care about the numbering scheme: a PATCH number tells an installer this is a safe drop-in, while an OTA image built against 0.1.0's table does not fit the new one. Stated as the **first** bullet of that release's `Known limitations` rather than left to the number. Revisit when the first version that has booted on hardware is cut. |

## Open holes (must be empty before a field release)

| # | Where | Hole | Consequence if shipped |
|---|-------|------|------------------------|
| 1 | `app.c`, `confirm_or_roll_back()` | `SPEC-DEVIATION(R-VER-08)` — the new image confirms itself with **no self-test** | A broken image marks itself valid and rollback never fires. This defeats the product's whole reason to exist. **Shipped in v0.1.0**, listed in that release's notes. |
| 2 | `updater.c`, `step_checking()` | Not implemented — never finds an update | The updater never updates. Fails safe, but does nothing. |
| 3 | `updater.c`, `step_downloading()` | Not implemented | Unreachable today. |
| 4 | `cfg.c`, `record_validate()` | No migration between record versions | Adding a field silently costs every deployed unit its stored settings. |
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
