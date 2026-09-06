---
title: Versioning and Release
category: rule
order: 5
purpose: Where the version number lives, what bumps it, and the order of a release.
status: active
updated: 2026-09-06
source: VERSION, CHANGELOG.md, workspace/0xF001/CMakeLists.txt, .github/workflows/release.yml
confidence: confirmed
keywords: VERSION, PROJECT_VER, CHANGELOG.md, docs/CHANGELOG, SemVer, Unreleased, esp_app_get_description
---

# Versioning and Release

> One number in one file; every other copy is derived from it, and the bump is the delivery step rather than paperwork after it.

## Do it with the script

```text
python docs/scripts/tool-release.py <version> --summary "<one line>" --dry-run
```

`tool-release.py` performs every numbered step below in order and refuses at
the first one that does not add up. Read the steps anyway — a script you do not
understand is one you cannot debug at the moment it stops half-way. Its
contract is in
[../interface/tool-release-cli.md](../interface/tool-release-cli.md).

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
   `## [Unreleased]` in the root `CHANGELOG.md`, grouped `Added` / `Changed` /
   `Fixed` / `Removed`, dropping the groups with nothing in them. Write it for
   whoever installs the release, not for whoever wrote it.
4. **Releasing moves that section out into its own file.** This repo splits the
   changelog one-file-per-version, which departs from the house standard's
   single-file shape — see
   [known-deviations.md](known-deviations.md). The move is:
   - Create `docs/CHANGELOG/v<version>.md`, headed `# <version> — <YYYY-MM-DD>`,
     holding the entries that were under `[Unreleased]`.
   - Add a row to the **Released** index table in the root `CHANGELOG.md`,
     newest first, linking that file.
   - Leave a fresh, empty `## [Unreleased]` in the root file and refresh the
     compare link at the bottom.

   The root file therefore only ever holds unreleased work plus the index; the
   history lives in `docs/CHANGELOG/`.
5. Bump in one commit that changes the single source and the changelog and
   nothing else, then tag on `main`. **A tag never moves** — it is what a support
   ticket maps back to.

   Pushing that tag runs `.github/workflows/release.yml`, which **refuses to
   publish** unless the tag matches `VERSION` and `docs/CHANGELOG/<tag>.md`
   exists. Steps 1-4 are therefore enforced, not merely documented.

   `main` refuses a direct push, so the commit reaches it through
   `release/*` → `developing` → `main`, merged rather than squashed so the
   release commit survives to be tagged.
6. Before tagging: rebuild and confirm the version the firmware reports is the
   one you typed. A mismatch means a copy escaped step 1.
7. Record the flash and RAM figures with each release and compare them with the
   previous one. `python docs/scripts/tool-esp.py size` prints both.
8. **Verify a rollback works before shipping the update mechanism.** An update
   path with no way back turns one bad release into a truck roll per unit.

## Current state

`VERSION` holds `0.1.0` and the root `CHANGELOG.md` carries every entry under
`[Unreleased]`. **`docs/CHANGELOG/` does not exist**, because nothing has been
released: the directory and its first file appear with the first tag.

**Nothing has been tagged.** `0.1.0` is a `VERSION` string, not a release. The
open holes in [known-deviations.md](known-deviations.md) are the reason it has
not been cut, and the release workflow would refuse the tag anyway until a
changelog file is prepared for it.

## See also

- [../architecture/ci-pipeline.md](../architecture/ci-pipeline.md) — the gates that enforce this
- [../architecture/build-and-toolchain.md](../architecture/build-and-toolchain.md) — how `VERSION` reaches the image
- [../data/flash-and-partitions.md](../data/flash-and-partitions.md) — why a partition change is MAJOR
