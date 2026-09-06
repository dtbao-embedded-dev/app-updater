---
title: Progress
updated: 2026-09-06
---

# Progress

> Current delivery state — what works, what's left, known issues. Update at every checkpoint (feature shipped, milestone, direction change).

## What works

- The full three-layer tree exists with six modules, each with one public
  header, one source file, and a component registration.
- **Verified by execution:** the update-cycle state machine and the status-code
  lookup compile clean on host GCC under the house warning set with `-Werror`,
  and every behavioural assertion passes — including the 2^32 ms clock wrap, the
  scheduling horizon rejection, and the repeatable stop/deinit contract.
- **Verified mechanically:** `clang-format` clean across all 24 sources; each
  symbol prefix declared in exactly one module directory; no upward include
  across a layer; no pin literal outside `driver/bsp/`; no source of ours under
  `workspace/`.
- `docs/scripts/tool-esp.py` resolves the repo root and the workspace correctly
  and refuses to run without `IDF_PATH`.

## What's left

1. **A self-test in `confirm_or_roll_back()`** — the highest-value hole. Until
   it exists, a broken image confirms itself and rollback never fires.
2. `updater` `CHECKING` step: fetch the manifest, compare versions, decide.
3. `updater` `DOWNLOADING` step: drive the fetch into the OTA write API, set the
   boot partition.
4. Network bring-up (Wi-Fi or Ethernet) — not in this repo at all.
5. Real board values in the BSP table, from the 0xF001 schematic.
6. A host test runner for the two Unity files.
7. Record migration in `storage`, before any field release.

## Known issues

- 🔴 **The ESP-IDF build has never been run.** No `IDF_PATH` was available in the
  session that wrote this repo, so the component names in `PRIV_REQUIRES`, the
  main-less build shape, and the partition table are all unverified. See
  [architecture/build-and-toolchain.md](architecture/build-and-toolchain.md).
- 🔴 `spec-verify` has never been run against this repo. It will at minimum flag
  the deviations in
  [rule/known-deviations.md](rule/known-deviations.md).
- ⚠ The two Unity test files are in no `SRCS`, so the firmware build passes
  without them — silently. A broken test is currently invisible.
- ⚠ The first build may fail on `-Wconversion`/`-Werror` in SDK macros expanded
  inside our translation units. That is the rule working; fix at the call site,
  never by silencing the warning.
