---
title: Versioning and Release
category: rule
order: 3
purpose: Where the version number lives, what bumps it, and the order of a release.
status: active
updated: 2026-09-06
source: VERSION, CHANGELOG.md, workspace/0xF001/CMakeLists.txt
confidence: confirmed
keywords: VERSION, PROJECT_VER, CHANGELOG.md, SemVer, Unreleased, esp_app_get_description
---

# Versioning and Release

> One number in one file; every other copy is derived from it, and the bump is the delivery step rather than paperwork after it.

## When this applies

Any change that ships, and any question of the form "what is running on that
unit".

## The rule

1. **The repo-root `VERSION` file is the single source of truth.** CMake reads
   it into `PROJECT_VER`, which lands in the image header, so what the firmware
   reports on boot is that file and cannot drift from it. Never type the number
   into a header, into `project()`, or into an artifact name.
2. Pick the component:
   - **MAJOR** — a stored layout or a wire protocol breaks compatibility. A
     change to the persisted record or to the partition table is MAJOR.
   - **MINOR** — a backward-compatible feature.
   - **PATCH** — a defect fixed with no interface change.
3. **Write the changelog entry the day the change is made**, under
   `## [Unreleased]`, grouped `Added` / `Changed` / `Fixed` / `Removed`, dropping
   the groups with nothing in them. Write it for whoever installs the release,
   not for whoever wrote it.
4. Releasing is a rename plus two inserts: `## [Unreleased]` becomes
   `## [<version>] - <YYYY-MM-DD>`, **a fresh empty `## [Unreleased]` goes back
   above it**, and the link definitions at the bottom are refreshed.
5. Bump in one commit that changes the single source and the changelog and
   nothing else, then tag on `main`. **A tag never moves** — it is what a support
   ticket maps back to.
6. Before tagging: rebuild and confirm the version the firmware reports is the
   one you typed. A mismatch means a copy escaped step 1.
7. Record the flash and RAM figures with each release and compare them with the
   previous one. `python docs/scripts/tool-esp.py size` prints both.
8. **Verify a rollback works before shipping the update mechanism.** An update
   path with no way back turns one bad release into a truck roll per unit.

## Current state

`VERSION` holds `0.1.0`; `CHANGELOG.md` has only an `[Unreleased]` section and
no released heading yet. Nothing has been tagged.

## See also

- [../architecture/build-and-toolchain.md](../architecture/build-and-toolchain.md) — how `VERSION` reaches the image
- [../data/flash-and-partitions.md](../data/flash-and-partitions.md) — why a partition change is MAJOR
