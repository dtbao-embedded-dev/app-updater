---
title: Active Context
updated: 2026-09-06
---

# Active Context

> What is being worked on right now. Read first every session; rewrite when the focus shifts. Transient — not a durable fact.

## Current focus

The repository skeleton is complete and documented. Nothing is under active
edit. The next meaningful move is **getting one real `idf.py build` to pass**,
which is what converts most of this bank from "written" to "verified".

## Recent changes

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
- Nothing has been committed to git yet — every file in the repo is untracked.
