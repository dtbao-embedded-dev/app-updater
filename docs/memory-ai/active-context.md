---
title: Active Context
updated: 2026-09-08
---

# Active Context

> What is being worked on right now. Read first every session; rewrite when the focus shifts. Transient — not a durable fact.

## Current focus

**A unit can now be asked why it died, and answer.** Seven tasks on
`release/v0.1`, each gated on a command rather than a reading, covering the
whole path from the panic handler to a backtrace on a laptop:

- A 64 KB `coredump` partition at `0xFF0000`, taken off the **tail** of
  `app_firmware` so not one existing offset moved.
- `driver/coredump` — four calls, no `init`/`deinit` and no instance, because
  there is no hardware to claim and the partition is found by subtype every
  time. That is also what lets the boot-time report run before any module is up.
- A new `0x07` opcode range: `DUMP_INFO` / `DUMP_READ` / `DUMP_ERASE`, with
  **no BEGIN and no END** — a read has no session to open, so a host may retry
  any chunk in any order.
- `report_last_panic()` at boot, logging the reason as text.
- `tool-usb.py dump FILE`, writing the file from a complete transfer **before**
  erasing the device.

Three things this cost, all written down rather than absorbed:

1. **The dispatcher's payload buffer is 32 bytes** (`PAYLOAD_MAX`, on a
   4096-byte task stack), so `DUMP_READ` stages its answer in
   `command_t.chunk` and uses the existing `*echo` route. 4 KB of `.bss`, and
   the reason the read chunk is a flash sector rather than the upgrade band.
2. **`FLASH_NO_OVERWRITE` makes the erase mandatory.** Keeping the first dump
   is right for a boot loop, but a unit read and not erased captures nothing
   further.
3. **A dump is worthless without the exact `.elf`.** Archived here; not yet in
   the sibling repo, which is where the likely crash lives.

**And it has still never run on hardware.** The bench session is still the next
one, and the core dump path is now one more thing that only a board can settle:

1. Fill the BSP board table from the real 0xF001 schematic — still
   placeholders, still able to drive a pin into something that does not like it.
2. Flash over **UART0** (the console moved there; USB auto-download is gone).
3. Cable in, and look for `USB\\VID_A331&PID_F001`.
4. `tool-usb.py ping`, `version`, a real `upgrade`, `boot-slot 1`, `restart`,
   `version` again.
5. **Force a panic**, let it reboot, read the boot log, then
   `tool-usb.py dump crash.bin` and `esp-coredump info_corefile
   --core-format raw -c crash.bin app_updater.elf`. A backtrace into `app.c` is
   the only thing that proves any of this works.

## Recent changes

- 2026-09-08 — **The core dump path, end to end in code.** Seven tasks, twelve
  commits. `driver/coredump` (T1), the `0x07` range in the map (T2), the three
  handlers and the 4 KB staging buffer (T3), `report_last_panic()` at boot (T4),
  `CONFIG_ESP_COREDUMP_FLASH_NO_OVERWRITE=y` (T5), `tool-usb.py dump` (T6) and
  the bank (T7). The host suite went from 83 tests to **94**. Two corrections
  worth keeping: `coredump_read` was changed mid-run to bound on the **stored
  dump length** rather than the partition, because the alternative was one
  full-dump SHA256 per chunk; and T4's planned check — a `report_last_panic`
  symbol in the map — was simply wrong for a `static` single-call-site function
  under `-Os`, so what proves it shipped is its log strings in the `.bin`.

- 2026-09-07 — **The board can now say why it died.** A `coredump` partition,
  64 KB at `0xFF0000`, plus `CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y`. The size
  is Espressif's recommended 64 KB, which covers an ELF dump of the task stacks
  but **not** `CONFIG_ESP_COREDUMP_CAPTURE_DRAM` (128 KB minimum) — that option
  stays off. The space came off the tail of `app_firmware`
  (`0xDE0000 → 0xDD0000`, 13.875 → 13.8125 MB) rather than from the two padding
  gaps, which are 24 KB and 12 KB and not contiguous: every other offset in the
  table, including both app slots, is byte-for-byte where it was. Reading a
  dump back off a field unit is still missing — see `progress.md` item 9b.

- 2026-09-07 — **The SDK left middleware, and a feature can be switched off.**
  Nine tasks on `release/v0.1`, each gated on a command rather than a reading:

  `driver/ota` took `esp_ota_*` and `esp_partition_*` out of
  `middleware/command` and `application/app` — eleven functions behind
  `ota_err_t` and an opaque `ota_session_t`. `bsp_restart()` and
  `bsp_mac_get()` took `esp_restart`, `esp_read_mac` and `vTaskDelay`.
  `fw_crc32_le()` in `middleware/fw` took `esp_rom_crc32_le` out of
  `protocol`, `cfg` and `command` for 128 bytes of image.
  `middleware/storage` moved to `driver/storage` with its own `storage_err_t`
  and took over `nvs_flash_init()` from its caller. `fw_config.h` added the two
  feature switches. The merged factory image became
  `bl_<project>_<pid>_<MonDDYY>.bin`, built from the `project()` token, the
  workspace name and the UTC date. The rule and both its exceptions went into
  the bank, and the README into R-RPO-08 order.

  **Three things were proved by mutation rather than assumed:** flipping one
  nibble of the new CRC table reddens the direct comparison against the
  independent implementation *and* every protocol frame test; deleting the
  read-compare-write guard in `storage_blob_save()` reddens exactly the wear
  test, at 11 writes instead of 1; `bsp_restart(0U)` reddens exactly the
  RESTART_APP ordering test.

  **Two things the run found that nobody asked about:** a `TODO` in
  `updater.c` instructing whoever writes the DOWNLOADING step to call
  `esp_ota_write()` directly — the exact thing the new rule forbids — and a
  `tool-esp.py --help` string still advertising the infer-the-only-workspace
  fallback that `resolve_workspace()` deliberately dropped. Both found by
  running a check, not by reading.

  **One deliberate ceiling:** the factory image name has a date but no time of
  day, so two builds on the same day overwrite each other silently. Marked with
  a `ponytail:` comment naming `-%H%M` as the upgrade.

- 2026-09-07 — **A settings library, and `storage` reduced to a byte pusher.**
  `middleware/cfg` holds `cfg_record_t` (the four settings behind a
  version/length/CRC envelope), the compiled-in defaults, and
  `cfg_<field>_get`/`_set` per setting. Persistence is **not** its business: it
  arrives as a `cfg_store_t` of two callbacks, which is what makes re-pointing
  the settings at the reserved `cfg_setting` partition a new adapter instead of
  an edit in `cfg`. `storage` lost the record entirely and gained
  `storage_blob_load`/`storage_blob_save` with a `STORAGE_BLOB_MAX` ceiling,
  which exists so the read-compare-write wear guard keeps a fixed compare
  buffer rather than a VLA on a task stack. **A setter writes nothing**, by
  design: the caller decides when a batch of changes is worth one flash write.
  Three pieces of validation are new, and the last of them —
  `check_interval_ms` refused at or past the scheduling horizon — used to reach
  `updater_init()` unchecked and **fail bring-up** on a CRC-valid record.

- 2026-09-07 — **`driver/bsp` has a per-chip port.** `BSP_USB_DP_GPIO`,
  `BSP_USB_DM_GPIO` and the two console pins left `bsp.h`. They are fixed in
  silicon, so this repo has no business restating them:
  `src/port/bsp_esp32s3.c` reads them from the chip's own `soc/` headers behind
  the new `bsp_priv.h` contract, and `driver/bsp/CMakeLists.txt` picks
  `src/port/bsp_${IDF_TARGET}.c` — failing with an instruction, not a missing
  source error, when a target has no port.

- 2026-09-07 — **`tool-esp.py` picks the product workspace instead of holding
  it.** Workspaces are the directories under `workspace/` that have a
  `CMakeLists.txt`, the answer is `WORKSPACE=` in `.env.esp`, and `-w NAME`
  overrides it per run. **Naming the product is required and there is no
  default**: the first version inferred it when the repo held exactly one
  workspace, which made the answer optional where a mistake is cheap and
  mandatory where it is not.

- 2026-09-07 — **The USB command channel.** CDC-ACM on USB-OTG at
  `0xA331:0xF001`, speaking the binary protocol from
  `data-monitor/data-mirror-firmware/docs/spec/usb`. `middleware/protocol`
  holds the frame codec and a 46-row opcode map; `middleware/command`
  dispatches to thirteen served handlers and answers `-7` for the 36 aimed at
  hardware this board does not have; `driver/usb_cdc` owns TinyUSB, the repo's
  first managed dependency. The console moved to UART0 because the S3 has one
  internal USB PHY and TinyUSB claims it.

- 2026-09-06 — Boot banner with the git commit, BSP asking the chip for its
  flash size, generated `sdkconfig` moved into `build/`, CPU at 240 MHz.
- 2026-09-06 — Flash is 16 MB, not 4 MB, and the partition table was rebuilt on
  that: `app_updater` 2 MB, `app_firmware` the 13.875 MB remainder, plus
  `cfg_factory` (4 KB) and `cfg_setting` (16 KB) inside the old alignment
  padding.
- 2026-09-06 — **v0.1.0 released**, cut end to end by `tool-release.py`. Its
  published assets cannot flash a blank board; fixed for the next tag, and the
  tag itself cannot be corrected.
- 2026-09-06 — History rewritten once to strip the `Co-Authored-By: Claude`
  trailer from 17 commits. The rule is in
  [rule/commit-messages.md](rule/commit-messages.md).
- 2026-09-06 — CI grown to six jobs, host harness added under `test/host/`,
  static analysis at a clean baseline, branch protection on `main` and
  `developing`, memory bank generated.

## Next steps

1. Fill the BSP board table from the real 0xF001 schematic. Do this **before**
   flashing: the current GPIO is a placeholder and may be wired to something
   that does not like being driven.
2. Flash and boot on hardware. That is the only thing that can confirm
   [data/flash-and-partitions.md](data/flash-and-partitions.md), which is still
   arithmetic rather than observation — and now also the only thing that can
   confirm `driver/ota`.
3. Wire the two layer-boundary greps from
   [rule/layer-boundaries.md](rule/layer-boundaries.md) into CI. Today the rule
   is enforced at review, which means it is enforced when someone remembers.
4. Run `spec-verify` and resolve or record what it finds.
5. Then the self-test in `confirm_or_roll_back()`, the hole that matters most —
   it shipped in v0.1.0 and is named in that release's notes.

## Active decisions

- **Middleware calls mapped drivers, not the vendor SDK** — stricter than the
  house standard's own table, which permits "HAL primitives". Two exceptions,
  both with written reasons: `esp_log.h`, and `middleware/ota_http` keeping
  `esp_http_client` because HTTP is a protocol stack rather than a device. See
  [rule/layer-boundaries.md](rule/layer-boundaries.md) and deviations 10 and 11
  in [rule/known-deviations.md](rule/known-deviations.md).
- **The feature switches are a plain header, not Kconfig.** A `CONFIG_*` symbol
  does not exist in the host test build, so `#if CONFIG_FEATURE_X` would read
  one way on target and the other way under test. The cost accepted: no
  `menuconfig` entry.
- **`test/host/stub/esp_rom_crc.h` is kept although it stubs nothing.** It is
  now the independent second opinion `fw_crc32_le()` is compared against; the
  protocol tests build frames with one implementation and the parser checks
  them with the other. Deleting it would collapse two opinions into one.
- **`from_storage_err()` clamps rather than casts.** Only the shared
  `-1 .. -19` range maps one for one; a code from the driver's own space
  (`-20` and below) arrives as `FW_ERR_IO` rather than as a number
  `fw_err_str()` cannot name.
- **`ota`'s `from_esp_err()` is silent**, unlike the other drivers': every
  caller inside the module already logs the raw vendor value with context the
  mapper does not have — which slot, how many bytes.
- **`CFG_CHECK_INTERVAL_MAX_MS` is a deliberate duplicate** of
  `UPDATER_HORIZON_MS` in `application/updater/src/updater.c`. `middleware/`
  may not include a header from `application/` (R-LAY-01), and validating the
  value where it arrives beats failing bring-up minutes later — so the number is
  restated one layer down. Change one and you must change the other; nothing
  goes red to say so.
- **`cfg` does not depend on `storage`.** It could have called it directly and
  saved two wrapper functions; the adapter exists so the `cfg_setting`
  partition stays reachable without editing either module.
- The USB dispatcher asks the application whether a slot write is in progress
  through a callback; it must not include `updater.h`, because `middleware/`
  may not include from `application/`.
- `PROTOCOL_MAX_DATA` stays at the spec's 32772 so a host built against the
  spec needs no change; the cost is two 32788-byte buffers, and
  `FW_FEATURE_USB_COMMAND` is now how a product declines to pay it.
- Commit messages carry no AI co-author or generation trailer. See
  [rule/commit-messages.md](rule/commit-messages.md).
- The project-wide status type is `fw_err_t` in `middleware/fw/`, **not**
  `updater_err_t` — that name would collide with the `updater` module prefix.
- Developer scripts live in `docs/scripts/`, a deliberate departure from the
  house standard's repo-root `scripts/`.
- ESP-IDF has no `main` component here; `application/app/` provides
  `app_main()` so that `workspace/` holds no source of ours.
- The changelog is split per version under `docs/CHANGELOG/`; the root file
  keeps `[Unreleased]` and the index only.
- `main` is push-protected with no bypass actor. Every change into it goes
  through a pull request from `developing`, self-merged (0 approvals required).
- The route to `main` is `release/*` → `developing` → `main`, merged and never
  squashed, because the tag has to land on a commit that survives on `main`.
