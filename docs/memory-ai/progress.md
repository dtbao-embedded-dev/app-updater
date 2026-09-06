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
- The repo is published: 8 commits on `main`, `developing` and `release/v0.1`,
  with branch protection on the first two **verified by an actual rejected
  push**, not just by the API response.
- A host test harness that actually runs: 13 Unity tests, green, built under the
  firmware's own `-Werror` warning set. **Proven to have teeth** — breaking the
  wrap-safe due check makes exactly one test go red, and restoring it makes the
  suite green again.
- Static analysis at a clean baseline, verified by running it locally **and in
  the CI container**: `cppcheck` 0 findings, `clang-tidy` 0 findings, no secrets
  in the tree. One real finding (a parameter that could be `const`) was found
  and fixed, not suppressed.
- **The firmware compiles.** `idf.py build` completes in `espressif/idf:v6.1`:
  1090 targets, `app_updater.bin` 198 KB against a 1.875 MB slot, 90% free.
- **v0.1.0 is released**, cut end to end by `tool-release.py`: seven phases, two
  merged pull requests, an annotated tag on `main`, and eight published
  artifacts. Both workflows green on the runs that produced it.

## What's left

1. **Fill the BSP board table from the 0xF001 schematic.** Its GPIO numbers are
   placeholders; driving the wrong pin is how a first bring-up damages a board.
2. **A self-test in `confirm_or_roll_back()`** — the highest-value hole. Until
   it exists, a broken image confirms itself and rollback never fires.
3. `updater` `CHECKING` step: fetch the manifest, compare versions, decide.
4. `updater` `DOWNLOADING` step: drive the fetch into the OTA write API, set the
   boot partition.
5. Network bring-up (Wi-Fi or Ethernet) — not in this repo at all.
6. **Flash and boot v0.1.0 on real hardware.** Nothing has ever executed on a
   board, so everything about the flash layout is still arithmetic.
7. An **on-target** smoke test. The host suite runs; nothing exercises a board.
8. Record migration in `storage`, before any field release.
9. Enable `gcc -fanalyzer` — deferred on purpose, not forgotten. The trigger is
   the first code that does buffer arithmetic, parsing, or allocation; see
   [rule/static-analysis.md](rule/static-analysis.md) for the exact list and the
   two-line change it takes.

## Known issues

- ⚠ The firmware has been **built** but never **flashed**. The partition table
  is accepted by the build and the image fits with 90% of its slot free; whether
  the layout boots on real hardware is still unknown.
- 🔴 `spec-verify` has never been run against this repo. It will at minimum flag
  the deviations in
  [rule/known-deviations.md](rule/known-deviations.md).
- ⚠ A test function that is not listed in `test/host/runner.c` compiles, links
  and never runs, with nothing going red to say so. The two lists must be kept
  in step by hand.
- ⚠ Everything now runs except the thing that matters most: **no code has ever
  executed on a board.** The image builds, is published, and is byte-addressable
  — and has never booted.
- ⚠ The first build may fail on `-Wconversion`/`-Werror` in SDK macros expanded
  inside our translation units. That is the rule working; fix at the call site,
  never by silencing the warning.
