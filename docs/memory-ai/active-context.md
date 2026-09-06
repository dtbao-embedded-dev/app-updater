---
title: Active Context
updated: 2026-09-06
---

# Active Context

> What is being worked on right now. Read first every session; rewrite when the focus shifts. Transient — not a durable fact.

## Current focus

CI runs and the firmware compiles. What is left untested is everything that
needs the two things a CI runner does not have: **a board** and **a tag**.
Nothing boots yet, and `release.yml` has never executed.

## Recent changes

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

1. Flash a real 0xF001 board. That is the only thing that can confirm
   [data/flash-and-partitions.md](data/flash-and-partitions.md), which is still
   arithmetic rather than observation.
2. Fill the BSP board table from the real schematic first — the current GPIO is
   a placeholder and may be wired to something else.
3. Run `spec-verify` and resolve or record what it finds.
4. Then the self-test in `confirm_or_roll_back()`, the hole that matters most.
5. `release.yml` stays unproven until a tag is pushed; consider a throwaway
   pre-release tag on a branch to exercise it before it matters.

## Active decisions

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
- No git tag exists. `v0.1.0` is a changelog file and a `VERSION` string, not a
  release.
