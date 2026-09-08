---
title: Progress
updated: 2026-09-08
---

# Progress

> Current delivery state — what works, what's left, known issues. Update at every checkpoint (feature shipped, milestone, direction change).

## What works

- The full three-layer tree exists with ten modules, each with one public
  header, at least one source file, and a component registration.
- **Verified by execution:** the update-cycle state machine and the status-code
  lookup compile clean on host GCC under the house warning set with `-Werror`,
  and every behavioural assertion passes — including the 2^32 ms clock wrap, the
  scheduling horizon rejection, and the repeatable stop/deinit contract.
- **Verified mechanically:** `clang-format` clean across all 43 sources; each
  symbol prefix declared in exactly one module directory; no upward include
  across a layer; no pin literal outside `driver/bsp/`; no source of ours under
  `workspace/`.
- `docs/scripts/tool-esp.py` resolves the repo root and the workspace correctly
  and refuses to run without `IDF_PATH`. The workspace is discovered rather than
  hardcoded — **verified by running the CLI in every state**: a bogus `-w`,
  `-w ../..`, nothing naming a workspace, `WORKSPACE=` in `.env.esp`, `-w`
  overriding it, and a typo in `WORKSPACE=`. Each resolution was proved by which
  workspace's
  `sdkconfig.defaults` the flash size came back from (16 MB vs 4 MB), not by the
  message alone. `format` still runs with an invalid `WORKSPACE=`, proving the
  resolution is lazy. The fresh-clone path was walked too: two workspaces and no
  `.env.esp` refuses **and writes the template**, the second run does not repeat
  that line, and filling `WORKSPACE=` in resolves. Naming the product is
  **required** - re-verified after that rule replaced the infer-the-only-one
  fallback: a blank `WORKSPACE=` refuses with exit 1, `-w` alone passes with no
  `.env.esp` at all (the CI shape, and no file is written), and `format` still
  runs with `WORKSPACE=` blank.
- The repo is published: 8 commits on `main`, `developing` and `release/v0.1`,
  with branch protection on the first two **verified by an actual rejected
  push**, not just by the API response.
- A host test harness that actually runs: **69 Unity tests, green**, built under
  the firmware's own `-Werror` warning set. **Proven to have teeth** — breaking
  the wrap-safe due check makes exactly one test go red, and restoring it makes
  the suite green again.
- Static analysis at a clean baseline, verified by running it locally **and in
  the CI container**: `cppcheck` 0 findings, `clang-tidy` 0 findings, no secrets
  in the tree. One real finding (a parameter that could be `const`) was found
  and fixed, not suppressed.
- **The firmware compiles.** `idf.py build` completes in `espressif/idf:v6.1`:
  1090 targets, `app_updater.bin` 0x31120 bytes (196 KB) against a 2 MB slot,
  90% free. Rebuilt clean on the 16 MB table at 240 MHz.
- **A USB command channel, host-verified end to end.** Three new modules
  (`middleware/protocol`, `middleware/command`, `driver/usb_cdc`) plus the wiring
  in `application/app`. The host suite went from 26 tests to 56, all green: the
  frame codec (resync past garbage, a `LENGTH` past the cap resuming one byte on,
  a bad CRC dropped silently, pad bytes inside the CRC), the dispatch order
  (`-2` before `-7` before `-3` before `-4`), every served handler, and the whole
  upgrade session (the chunk band, sequential offsets, the 1024 rule and its
  last-chunk exemption, a wrong image CRC refusing to finalise, and `UPG_BEGIN`
  freeing the previous OTA handle).
- **Three of those tests were proved to have teeth by mutation**, not assumed:
  changing the parser's resync rule reddens exactly the resync test; reporting a
  wrong `LENGTH` as `-4` reddens exactly the length test; dropping the
  free-the-old-handle call in `UPG_BEGIN` reddens exactly the two open-session
  assertions.
- `docs/scripts/tool-usb.py` speaks the protocol from a PC. Its `selftest` runs
  with no board and no pyserial, and pins the CRC to the same fixed vector the
  firmware tests use.
- **Measured, not estimated:** the channel costs ~65.6 KB of static RAM (two
  32788-byte frame buffers); `.bss` is 70,592 bytes, 20.66 % of DRAM, leaving
  roughly 270 KB for the heap. `app_updater.bin` is 0x3a7f0 with 89 % of its
  2 MB slot free.
- **v0.1.0 is released**, cut end to end by `tool-release.py`: seven phases, two
  merged pull requests, an annotated tag on `main`, and eight published
  artifacts. Both workflows green on the runs that produced it.
- **Middleware calls mapped drivers, not the vendor SDK.** Verified by the two
  greps in [rule/layer-boundaries.md](rule/layer-boundaries.md), not by
  reading: no vendor include in any middleware source but `esp_log.h` and
  `ota_http`'s HTTP client, and no `esp_ota_*` / `esp_partition_*` / `nvs_*` /
  `esp_restart` / `esp_read_mac` / `esp_rom` anywhere in `middleware/` or
  `application/` outside two lines of deliberate comment prose. `driver/` holds
  four modules now: `bsp`, `ota`, `storage`, `usb_cdc`.
- **83 host tests, green**, up from 69, built under the firmware's own
  `-Werror` warning set. Eleven of the new ones are the blob store's first
  tests ever. **Three assertions were proved to have teeth by mutation**, not
  assumed: one wrong nibble in the CRC table reddens both the direct comparison
  against an independent implementation and every protocol frame test; deleting
  the read-compare-write guard reddens exactly the wear test at 11 writes
  instead of 1; `bsp_restart(0U)` reddens exactly the RESTART_APP ordering test.
- **A feature can be compiled out, measured rather than assumed.**
  `FW_FEATURE_USB_COMMAND` at 0 builds clean and frees **67 688 bytes of
  `.bss`** — 20.66 % of DRAM down to 0.85 % — plus 41.4 KB of flash;
  `FW_FEATURE_UPDATER` at 0 builds clean and leaves `updater_step` with no
  address at all in `app_updater.map`. Both restored to 1 and the baseline size
  returns exactly.
- **The factory image says what it is.**
  `tool-esp.py merge` produces `bl_app_updater_0xF001_Sep0726.bin`, every field
  read from the build; `release.yml` copies it by glob and fails unless exactly
  one matches.
- **A settings library with a persistence seam, `middleware/cfg`.** It owns the
  record, the defaults and one validated get/set pair per setting; where the
  bytes go arrives as a two-callback adapter, so `cfg` names no storage
  technology and the blob store it forwards to is now `driver/storage`. One of
  its tests was **proved to have teeth by mutation**: deleting the
  `CFG_CHECK_INTERVAL_MAX_MS` guard from the setter reddens exactly
  `test_cfg_check_interval_refuses_the_scheduling_horizon` and nothing else.
  Restoring it returns the suite to green.
- **`cfg` is in the image, not just in the build.** Verified by reading
  `app_updater.map`: `cfg_init`, `cfg_record_default` and
  `cfg_check_interval_ms_get` carry real addresses, and `app_updater.bin` grew
  from 0x3a820 to 0x3ac20 when `app` started requiring the component.
- **`driver/bsp` has a per-chip port.** The silicon-fixed USB and console pins
  left `bsp.h` for `src/port/bsp_esp32s3.c`, which reads them from the chip's
  own `soc/` headers; `driver/bsp/CMakeLists.txt` selects the port by
  `IDF_TARGET` and refuses with an instruction when a target has none.

- **A core dump partition, built and enabled.** 64 KB at `0xFF0000`, taken off
  the tail of `app_firmware` so not one existing offset moved.
  `CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y`; ELF format, SHA256 checksum,
  `CONFIG_ESP_COREDUMP_CAPTURE_DRAM` off. **Verified by the build, not by
  arithmetic alone:** ESP-IDF v6.1's `gen_esp32part.py` accepts the CSV with no
  warning, the generated `partition_table/partition-table.bin` reads back with
  `coredump,data,coredump,0xff0000,64K` and `app_firmware` as `14144K` ending
  exactly at `0xFF0000`, and `libespcoredump.a` costs 9 792 bytes (8 267 flash
  text, 617 flash data, 908 IRAM) with `CONFIG_ESP_COREDUMP_STACK_SIZE=0`, so
  no dedicated DRAM stack. `app_updater.bin` is 0x3f620, 88% of its slot free.
  The `build/sdkconfig` trap was walked, not assumed: the edit to
  `sdkconfig.defaults` only took effect after `build/` was removed.

- **A core dump can be fetched off a running unit, end to end in code.** The
  64 KB partition at `0xFF0000`, `driver/coredump` as the only module naming
  `esp_core_dump_*`, three opcodes in a new `0x07` range, the boot log naming
  the panic reason, and `tool-usb.py dump FILE`. **Verified by execution at
  every step, not by reading:** the served-opcode test was red at 10 vs 13
  before the handlers existed, the ten new handler tests were red with `Was -7`
  (map served, no `case`), and the host suite is **94 tests, all green**.
  `tool-esp.py build` is clean under `-Werror`, `app_updater.bin` is `0x40df0`
  of a `0x200000` slot (87 % free), and `.bss` is 75 504 bytes (22.09 % of
  DRAM) including the 4096-byte `DUMP_READ` staging buffer.
- **Three of those tests were proved to have teeth by mutation.** Reading from
  offset 0 instead of the requested offset left two of three content assertions
  green, because the first fill pattern repeated with period 256 and every
  offset under test was a multiple of 256. Folding the high byte of the index
  in turns all three red under the same mutation. A test that passes for the
  wrong reason was found by trying, not by inspection.
- **`DUMP_READ` could not use the dispatcher's payload buffer at all.**
  `PAYLOAD_MAX` is 32 bytes on the stack of a task with a 4096-byte stack, so
  the answer is staged in `command_t.chunk` and handed to the existing `*echo`
  route — the same mechanism PING uses for the request's own bytes. That
  constraint is why the read chunk is a flash sector and not the upgrade band.

## What's left

1. **Fill the BSP board table from the 0xF001 schematic.** Its GPIO numbers are
   placeholders; driving the wrong pin is how a first bring-up damages a board.
2. **A self-test in `confirm_or_roll_back()`** — the highest-value hole. Until
   it exists, a broken image confirms itself and rollback never fires.
3. `updater` `CHECKING` step: fetch the manifest, compare versions, decide.
4. `updater` `DOWNLOADING` step: drive the fetch into `ota_session_write()`,
   then `ota_boot_slot_set()`. The USB path already runs those session rules in
   `middleware/command/src/command_upgrade.c`; the HTTP path should reuse them
   rather than growing a second set. **Never `esp_ota_*` directly** — see
   [rule/layer-boundaries.md](rule/layer-boundaries.md).
5. Network bring-up (Wi-Fi or Ethernet) — not in this repo at all.
6. **Flash and boot v0.1.0 on real hardware.** Nothing has ever executed on a
   board, so everything about the flash layout is still arithmetic.
7. An **on-target** smoke test. The host suite runs; nothing exercises a board.
8. Record migration in `middleware/cfg` (`record_validate()` carries the
   TODO), before any field release.
8b. **Wire the two layer-boundary greps into CI.** Today the rule in
   [rule/layer-boundaries.md](rule/layer-boundaries.md) is enforced at review,
   which means it is enforced when someone remembers. It is the only new rule
   in the repo with no automated gate.
9b. **Archive `app_firmware`'s `.elf` when that repo ships.** The read-out path
   exists now, but a dump is only decodable against the exact build that
   crashed, and a crash there is the likely one — it is the product and it runs
   almost all the time. `release.yml` here archives `app_updater.elf` and the
   bootloader's pair, and its `SHA256SUMS` doubles as the dump-to-release index
   because `app_elf_sha256` **is** `sha256(app_updater.elf)`. The sibling repo
   is empty and its first release has to do the same, or a dump fetched from
   the field is bytes with nothing to decode them against.
9c. **Prove the core dump path on hardware.** Force a panic, let it reboot,
   check the boot log names the reason, then `tool-usb.py dump crash.bin` and
   `esp-coredump info_corefile --core-format raw` must print a backtrace into
   `app.c`. Nothing before that step proves the feature — only the plumbing.
9. Enable `gcc -fanalyzer` — deferred on purpose, not forgotten. The trigger is
   the first code that does buffer arithmetic, parsing, or allocation; see
   [rule/static-analysis.md](rule/static-analysis.md) for the exact list and the
   two-line change it takes.

## Known issues

- ⚠ **The USB channel has never enumerated on a board.** Every byte of it is
  proved on the host, and the descriptor, the PHY switch and the COM port are
  the parts a host test cannot reach. First thing to check on the bench:
  `USB\VID_A331&PID_F001` in Device Manager, then
  `python docs/scripts/tool-usb.py ping`.
- ⚠ **Flashing changed.** The console moved to UART0 because the USB command
  channel claims the single internal PHY, so there is no USB auto-download reset
  any more: flash over a UART0 bridge, or hold BOOT. Anyone following the older
  instructions will conclude the board is dead when it is not.

- ⚠ The firmware has been **built** but never **flashed**. The partition table
  is accepted by the build and the image fits with 90% of its slot free; whether
  the layout boots on real hardware is still unknown.
- ⚠ **No core dump has ever been written or read.** The partition, the config,
  the driver, the three opcodes, the boot-time report and the host subcommand
  are all in place and all proved on the host, but only a real panic on real
  silicon writes a dump — so nothing yet shows that `esp_partition_read` on
  this partition returns what `espcoredump` wrote. Until that bench run the
  whole feature is plumbing.
- ⚠ **`DUMP_ERASE` is load-bearing and easy to forget.**
  `CONFIG_ESP_COREDUMP_FLASH_NO_OVERWRITE` is on, so a unit whose dump is read
  but never erased **captures no further panic for the rest of its life**.
  `tool-usb.py dump` erases by default and `--keep` is the deliberate opt-out,
  but a host that dies between the last `DUMP_READ` and the `DUMP_ERASE` leaves
  the unit in exactly that state. Recoverable by running `dump` again.
- 🔴 `spec-verify` has never been run against this repo. It will at minimum flag
  the deviations in
  [rule/known-deviations.md](rule/known-deviations.md).
- ⚠ A test function that is not listed in `test/host/runner.c` compiles, links
  and never runs, with nothing going red to say so. The two lists must be kept
  in step by hand.
- ⚠ **v0.1.0's published assets cannot flash a blank board** — see
  [rule/known-deviations.md](rule/known-deviations.md). Fixed for the next tag;
  the tag itself cannot be corrected.
- ⚠ Everything now runs except the thing that matters most: **no code has ever
  executed on a board.** The image builds, is published, and is byte-addressable
  — and has never booted.
- ⚠ The first build may fail on `-Wconversion`/`-Werror` in SDK macros expanded
  inside our translation units. That is the rule working; fix at the call site,
  never by silencing the warning.

- ⚠ **Nothing in the firmware calls `cfg_save()`.** Settings are read at boot
  and never written, so the linker drops `cfg_save` from the image. The API and
  its tests exist; the first writer will be whatever records
  `last_ok_fw_version` or `boot_fail_count`. Until then a setting changed at
  runtime is lost on reset. `storage_blob_save()` now has host tests either
  way, including the wear guard, so the persistence half is no longer unproven
  — only uncalled.
- ⚠ **cppcheck is not version-pinned, and the versions differ.** CI runs
  **2.13.0** (apt, in `espressif/idf:v6.1`); a developer machine here has
  **2.20.0**. They do not report the same findings, so `analyse` can be clean
  locally and red in CI or the other way round. `ci.yml` prints
  `cppcheck --version` for exactly this reason - check it before believing a
  local result. `clang-format` and `clang-tidy` are pinned; cppcheck publishes
  no wheel and no official Linux binary, and an apt pin breaks when the
  container base moves.
- ⚠ **`analyse` reports cppcheck findings as `style:`, not `error:`.** Its real
  signal is the **exit code**: `python docs/scripts/tool-esp.py analyse; echo $?`.
  Grepping the output for `error:` reports a clean run on a tree with findings -
  which is how three of them reached CI on 2026-09-07 while a local check said
  clean.
- ⚠ **The factory image name has no time of day.** Two builds on the same day
  produce the same `bl_..._Sep0726.bin` and the second overwrites the first
  without a word. Deliberate, marked with a `ponytail:` comment in
  `tool-esp.py` naming `-%H%M` as the upgrade.
- ⚠ **`driver/ota` is new code on the path that writes flash and has never run
  on a board.** Its host fake proves the contract; only hardware proves the
  implementation.
- ⚠ `application/updater/CMakeLists.txt` still declares `PRIV_REQUIRES
  ota_http app_update esp_partition`, none of which `updater.c` includes. The
  dead `storage` entry went when that module moved to `driver/`; these three
  stay because the CHECKING and DOWNLOADING steps are written against them.
  They are a lie until those steps exist.
