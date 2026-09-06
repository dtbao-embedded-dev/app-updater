---
title: Active Context
updated: 2026-09-06
---

# Active Context

> What is being worked on right now. Read first every session; rewrite when the focus shifts. Transient — not a durable fact.

## Current focus

Nothing is under active edit. The next meaningful move is **getting one real CI
run to go green**: the firmware has never been compiled by ESP-IDF and neither
workflow has ever executed, so the whole target-side story is written rather
than observed. Everything that could be verified on a PC has been.

## Recent changes

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

1. Export ESP-IDF 6.x and run `python docs/scripts/tool-esp.py build`. Expect
   the first failures in component names and `-Wconversion`.
2. On success, flip
   [architecture/build-and-toolchain.md](architecture/build-and-toolchain.md)
   and [data/flash-and-partitions.md](data/flash-and-partitions.md) from
   `inferred` to `confirmed`.
3. Run `spec-verify` and resolve or record what it finds.
4. Fill the BSP board table from the real schematic.
5. Then start on the self-test in `confirm_or_roll_back()`.

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
