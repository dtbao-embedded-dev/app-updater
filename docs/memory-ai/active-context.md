---
title: Active Context
updated: 2026-09-07
---

# Active Context

> What is being worked on right now. Read first every session; rewrite when the focus shifts. Transient — not a durable fact.

## Current focus

The **settings now have a library of their own.** `middleware/cfg` owns the
record, its defaults and one validated get/set pair per setting;
`middleware/storage` was cut down to an opaque NVS blob store, and the two
adapter wrappers in `application/app` are the only place left that knows the
settings live in NVS. 69 host tests green, firmware builds, `cfg` verified
present in `app_updater.map`.

**What that leaves open, deliberately:** nothing calls `cfg_save()` yet, so a
setting changed at runtime is still lost on reset. That was equally true before
— `storage_record_save()` had no caller either — but the API and its tests now
exist, so the first writer is cheap to add.

The **USB command channel** is written and green: a clean firmware build and
three mutation checks proving its tests bite. A PC can now read a unit and push
an image into `app_firmware` without a network.

**And it has still never run on hardware.** That is unchanged and now matters
more, because the channel added two things a host test cannot reach: the USB
descriptor a PC has to accept, and the PHY switch that takes USB-Serial-JTAG
away. The next session is a bench session, in this order:

1. Fill the BSP board table from the real 0xF001 schematic — still placeholders,
   still able to drive a pin into something that does not like it.
2. Flash over **UART0** (the console moved there; USB auto-download is gone).
3. Cable in, and look for `USB\VID_A331&PID_F001`. If it does not appear, the
   descriptor or the PHY switch is where to look, not the protocol.
4. `tool-usb.py ping`, then `version`, then a real `upgrade` of an
   `app_firmware` image, then `boot-slot 1` and `restart`, then `version` again
   to confirm the new image is what booted. That last step is the only one that
   proves the upgrade worked.

## Recent changes

- 2026-09-07 — **A settings library, and `storage` reduced to a byte pusher.**
  `middleware/cfg` holds `cfg_record_t` (the four settings behind a
  version/length/CRC envelope), the compiled-in defaults, and
  `cfg_<field>_get`/`_set` per setting. Persistence is **not** its business: it
  arrives as a `cfg_store_t` of two callbacks, which is what makes re-pointing
  the settings at the reserved `cfg_setting` partition a new adapter instead of
  an edit in `cfg`. `middleware/storage` lost the record entirely — no
  `storage_record_t`, no CRC, no `esp_rom` — and gained
  `storage_blob_load`/`storage_blob_save` with a `STORAGE_BLOB_MAX` ceiling,
  which exists so the read-compare-write wear guard keeps a fixed compare
  buffer rather than a VLA on a task stack. `application/app` holds a `cfg_t`
  and supplies the two wrappers. **A setter writes nothing**, by design: the
  caller decides when a batch of changes is worth one flash write, and a
  call-counting fake store asserts that negative. Three pieces of validation
  are new: both stored strings must be terminated inside their own field (only
  the URL was checked), a refused value leaves the old one in place, and
  `check_interval_ms` is refused at or past the scheduling horizon — that last
  one used to reach `updater_init()` unchecked and **fail bring-up** on a
  CRC-valid record. No migration was written and none is needed: layout and NVS
  key are unchanged.

- 2026-09-07 — **`driver/bsp` has a per-chip port.** `BSP_USB_DP_GPIO`,
  `BSP_USB_DM_GPIO` and the two console pins left `bsp.h`. They are fixed in
  silicon, so this repo has no business restating them: `src/port/bsp_esp32s3.c`
  reads `USBPHY_DP_NUM`, `USBPHY_DM_NUM`, `U0TXD_GPIO_NUM` and `U0RXD_GPIO_NUM`
  from the chip's own `soc/` headers behind the new `bsp_priv.h` contract, and
  `driver/bsp/CMakeLists.txt` picks `src/port/bsp_${IDF_TARGET}.c` — failing
  with an instruction, not a missing-source error, when a target has no port.
  A part with no internal USB PHY gets a port that reports `BSP_GPIO_NONE`,
  not a `bsp.c` that fails to compile. `usb_pins.h` exists only for esp32s2 and
  esp32s3, which is exactly why that include sits in the port.

- 2026-09-07 — **`tool-esp.py` picks the product workspace instead of holding
  it.** `WORKSPACE = REPO / "workspace" / "0xF001"` is gone; workspaces are the
  directories under `workspace/` that have a `CMakeLists.txt`, the answer is
  `WORKSPACE=` in `.env.esp`, and `-w NAME` overrides it per run. The listing on
  disk is deliberately the only list — enumerating products in `.env.esp` too
  would be the copy that goes stale. **Naming the product is required and there
  is no default**: the first version inferred it when the repo held exactly one
  workspace, which made the answer optional where a mistake is cheap and
  mandatory where it is not. So `.env.esp` here now says `WORKSPACE=0xF001`, and
  `ci.yml`/`release.yml` pass `-w 0xF001` on their five build steps — the only
  place either workflow states which product it builds.
  Resolution is lazy so `format`/`test`/`analyse`, which cover the whole repo,
  never ask. The refusal sentence is one function, `workspace_refusal()`, that
  the `.env.esp` template interpolates as a worked two-product example - so the
  file a developer has to fill in cannot end up quoting a message the script no
  longer prints, and that refusal creates the file when a fresh clone has none.
  Still open: `.github/workflows/release.yml` hardcodes
  `workspace/0xF001/build` in its artifact step, and `merge` names the image
  `app-updater-v<VERSION>-factory.bin` with no product in it — neither matters
  with one product, both need a decision with two.

- 2026-09-07 — **The USB command channel.** CDC-ACM on USB-OTG at
  `0xA331:0xF001`, speaking the binary protocol from
  `data-monitor/data-mirror-firmware/docs/spec/usb`. `middleware/protocol` holds
  the frame codec and a 46-row opcode map; `middleware/command` dispatches to
  ten served handlers and answers `-7` for the 36 aimed at hardware this board
  does not have; `driver/usb_cdc` owns TinyUSB, the repo's first managed
  dependency, since ESP-IDF v6.1 ships no USB device stack. The console moved to
  UART0 because the S3 has one internal USB PHY and TinyUSB claims it. Also
  `docs/scripts/tool-usb.py` and `fullclean` in `tool-esp.py`. Two findings
  recorded as deviations: the spec's `PING` ceiling contradicts its own cap, and
  the 36 unsupported opcodes are a deliberate `-7` rather than stubs.

- 2026-09-06 — **Boot banner, and the BSP now asks the chip.** `app_run()`
  opens with `print_banner()` — printf, not ESP_LOGI — carrying the git
  commit — the full 40-character hash, a PRIVATE compile definition from
  `application/app/CMakeLists.txt` —  chip model/revision/features, core count, clock and MAC. `bsp_init()` reads
  `flash_size_bytes` from `esp_flash_get_size()` instead of a compiled-in
  constant, so the board table holds only what the schematic decides. The
  generated `sdkconfig` moved into `build/` (`SDKCONFIG` in the workspace
  CMakeLists) so an edit to `sdkconfig.defaults` can no longer be silently
  ignored. `@author` headers and the unused per-module `*_VERSION_MAJOR/
  MINOR/PATCH` macros were removed. Verified by a clean rebuild: 1090
  targets, no warnings under `-Werror`. CPU raised to 240 MHz
  (`CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240`), verified in the regenerated
  `build/sdkconfig`.

- 2026-09-06 — **Flash is 16 MB, not 4 MB**, and the table was rebuilt on that.
  Every partition now starts at `0xF000`, leaving `0x9000 .. 0xF000` (24 KB)
  reserved and empty behind the table. `app_updater` is 2 MB, `app_firmware`
  the 13.875 MB remainder — the two app slots are no longer the same size.
  Two raw data partitions were added, `cfg_factory` (4 KB) and `cfg_setting`
  (16 KB), both inside the old alignment padding at no cost to either slot.
  `CONFIG_ESPTOOLPY_FLASHSIZE_16MB` and the BSP board table follow.
  Verified with ESP-IDF v6.1's `gen_esp32part.py`, not on hardware. See
  [data/flash-and-partitions.md](data/flash-and-partitions.md).

- 2026-09-06 — **History rewritten once** to strip the
  `Co-Authored-By: Claude ...` trailer from the 17 commits that carried it, on
  `main`, `developing` and `release/v0.1`. `filter-branch --msg-filter` only;
  trees verified identical, commit counts unchanged. Tag `v0.1.0` was recreated
  and now sits on `38fb9507`. The rule is recorded in
  [rule/commit-messages.md](rule/commit-messages.md): no AI attribution trailer
  in a commit message here, ever again.

- 2026-09-06 — Found by reading v0.1.0's own assets back: the published set
  could not flash a blank board. `release.yml` now ships a merged factory
  image, `ota_data_initial.bin` and `sdkconfig`, flattens the `flash_args`
  paths, and fails if either half of the defect returns.

- 2026-09-06 — **v0.1.0 released.** Cut with `docs/scripts/tool-release.py`:
  seven phases, exit 0. `release.yml` ran for the first time and passed, both
  gates holding. Tag `v0.1.0` is annotated, on `38fb9507`, an ancestor of
  `main`; eight artifacts published. `main` was never pushed to directly.
- 2026-09-06 — `tool-release.py` added: the release procedure as one command,
  with a pre-flight that refuses before writing anything.

- 2026-09-06 — First real CI run (34031391115): 4/6 green. Two genuine failures
  found and fixed — `-Wundef` cannot coexist with ESP-IDF's log headers, and
  `pip` is not on PATH in the IDF container until `export.sh` is sourced. Both
  fixes reproduced and verified in the same image locally before re-pushing.
- 2026-09-06 — Firmware built for the first time: 1090 targets,
  `app_updater.bin` 198 KB, 90% of its slot free.

- 2026-09-06 — CI grown to the six jobs R-SAN asks for: format, cppcheck +
  clang-tidy, host tests, ASan/UBSan, firmware build, gitleaks. Tool versions
  pinned. Baseline verified clean by running each locally.
- 2026-09-06 — Host test harness added under `test/host/`; `tool-esp.py` gained
  `test` and `analyse`. The wrap test was found to pass against a broken
  implementation and was rewritten until a mutation made it fail.
- 2026-09-06 — Partition labels renamed to `app_updater` / `app_firmware`: the
  two app slots now hold two different programs, which costs the updater its own
  rollback slot.
- 2026-09-06 — README rewritten with badges in the R-RPO-08 section order.
- 2026-09-06 — `docs/CHANGELOG/` removed again: nothing is released, so every
  entry is back under `[Unreleased]`.

- 2026-09-06 — Changelog split: the root `CHANGELOG.md` now holds only
  `[Unreleased]` plus a released-version index; `docs/CHANGELOG/v0.1.0.md` holds
  the first entry. Recorded as a deviation from R-VER-13.
- 2026-09-06 — Branch protection enabled on GitHub: `main` requires a pull
  request (0 approvals) and blocks force-push and deletion; `developing` blocks
  force-push and deletion. No bypass actors, so the owner is bound too.
  Default branch moved from `developing` to `main`.
- 2026-09-06 — First 8 commits pushed; branches `main`, `developing` and
  `release/v0.1` all published and tracking.
- 2026-09-06 — Memory bank generated: 20 durable docs across the five
  categories, from the source as written.
- 2026-09-06 — `scripts/fw.py` moved to `docs/scripts/` and renamed
  `tool-esp.py`; every reference updated.
- 2026-09-06 — Repository scaffolded from scratch against the house embedded-C
  standard: six modules, the 0xF001 build entry, format config, git hook,
  README and changelog.

## Next steps

1. Fill the BSP board table from the real 0xF001 schematic. Do this **before**
   flashing: the current GPIO is a placeholder and may be wired to something
   that does not like being driven.
2. Flash and boot `v0.1.0`. That is the only thing that can confirm
   [data/flash-and-partitions.md](data/flash-and-partitions.md), which is still
   arithmetic rather than observation.
3. Run `spec-verify` and resolve or record what it finds.
4. Then the self-test in `confirm_or_roll_back()`, the hole that matters most —
   it shipped in v0.1.0 and is named in that release's notes.

## Active decisions

- **`CFG_CHECK_INTERVAL_MAX_MS` is a deliberate duplicate** of
  `UPDATER_HORIZON_MS` in `application/updater/src/updater.c`. `middleware/` may
  not include a header from `application/` (R-LAY-01), and validating the value
  where it arrives beats failing bring-up minutes later — so the number is
  restated one layer down. Change one and you must change the other; nothing
  goes red to say so.
- **`cfg` does not depend on `storage`.** It could have called it directly and
  saved two wrapper functions; the adapter exists so the `cfg_setting`
  partition stays reachable without editing either module. That is the one place
  in this change where a seam was chosen over the shorter diff, and the reason
  is written down here rather than assumed obvious.

- The USB dispatcher asks the application whether a slot write is in progress
  through a callback; it must not include `updater.h`, because `middleware/`
  may not include from `application/`. The plan for this work originally had it
  holding an `updater_t *`, which would have broken that.
- `PROTOCOL_MAX_DATA` stays at the spec's 32772 so a host built against the spec
  needs no change; the cost is two 32788-byte buffers, and the third the spec
  budgets was removed by building a reply body straight into the reply frame.

- Commit messages carry no AI co-author or generation trailer. See
  [rule/commit-messages.md](rule/commit-messages.md).
- The project-wide status type is `fw_err_t` in `middleware/fw/`, **not**
  `updater_err_t` — that name would collide with the `updater` module prefix.
- Developer scripts live in `docs/scripts/`, a deliberate departure from the
  house standard's repo-root `scripts/`. Recorded in
  [rule/known-deviations.md](rule/known-deviations.md).
- ESP-IDF has no `main` component here; `application/app/` provides `app_main()`
  so that `workspace/` holds no source of ours.
- The changelog is split per version under `docs/CHANGELOG/`; the root file
  keeps `[Unreleased]` and the index only.
- `main` is push-protected with no bypass actor. Every change into it goes
  through a pull request from `developing`, self-merged (0 approvals required).
- New work goes on `developing`. `release/v0.1` did its job and is closed; the
  next release cuts a fresh `release/*` branch.
- The route to `main` is `release/*` → `developing` → `main`, merged and never
  squashed, because the tag has to land on a commit that survives on `main`.
