---
title: Progress
updated: 2026-09-07
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
- **A settings library with a persistence seam, `middleware/cfg`.** It owns the
  record, the defaults and one validated get/set pair per setting; where the
  bytes go arrives as a two-callback adapter, so `cfg` names no storage
  technology and `middleware/storage` shrank to an opaque NVS blob store. The
  host suite went from 56 tests to **69, all green**, and one of the new ones
  was **proved to have teeth by mutation**: deleting the
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

## What's left

1. **Fill the BSP board table from the 0xF001 schematic.** Its GPIO numbers are
   placeholders; driving the wrong pin is how a first bring-up damages a board.
2. **A self-test in `confirm_or_roll_back()`** — the highest-value hole. Until
   it exists, a broken image confirms itself and rollback never fires.
3. `updater` `CHECKING` step: fetch the manifest, compare versions, decide.
4. `updater` `DOWNLOADING` step: drive the fetch into the OTA write API, set the
   boot partition. The USB path already does this work in
   `middleware/command/src/command_upgrade.c`; the HTTP path should reuse the
   same session rules rather than growing a second set.
5. Network bring-up (Wi-Fi or Ethernet) — not in this repo at all.
6. **Flash and boot v0.1.0 on real hardware.** Nothing has ever executed on a
   board, so everything about the flash layout is still arithmetic.
7. An **on-target** smoke test. The host suite runs; nothing exercises a board.
8. Record migration in `middleware/cfg` (`record_validate()` carries the
   TODO), before any field release.
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
  and never written — unchanged from before the refactor, when
  `storage_record_save()` had no caller either — so the linker drops
  `cfg_save` from the image. The API and its tests exist; the first writer will
  be whatever records `last_ok_fw_version` or `boot_fail_count`. Until then a
  setting changed at runtime is lost on reset.
- ⚠ `application/updater/CMakeLists.txt` still declares `PRIV_REQUIRES ...
  storage`, but `updater.c` includes no header of it. A dead dependency, found
  while wiring `cfg` and deliberately left alone: removing it is not this
  change's business.
