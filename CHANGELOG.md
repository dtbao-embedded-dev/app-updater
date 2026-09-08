# Changelog

All notable changes to this project, SemVer, single source of truth in
`VERSION` (R-VER-01).

An entry lands under `[Unreleased]` **the day the change is made**, not at
release time (R-VER-05). Releasing moves that whole section into its own file
under `docs/CHANGELOG/v<version>.md`, adds a row to the **Released** index
below, and leaves a fresh empty `[Unreleased]` here.

## [Unreleased]

### Fixed
- **A version bump did not reach a local build.** `workspace/0xF001/CMakeLists.txt`
  read the repo-root `VERSION` with `file(READ)`, which does **not** register a
  CMake dependency on what it reads - so after a bump the cache kept the old
  `PROJECT_VER`, ninja had no reason to re-run cmake, and the image reported the
  previous number while `VERSION` read the new one. Named in
  `CMAKE_CONFIGURE_DEPENDS` now. **Found by following the release rule's own
  step 6** during the v0.1.1 cut: it reported `App version: 0.1.0` against a
  `VERSION` of `0.1.1`. CI and the release workflow never saw it, because a
  fresh checkout configures from scratch - the published v0.1.1 image reports
  `0.1.1` correctly, so this shipped nothing wrong and only cost local time.
  That is also why step 6's diagnosis was misleading: it blamed "a copy escaped
  step 1" and sent the reader hunting for a hardcoded number that does not
  exist. Both the rule and the build doc now name the real cause.

_Nothing yet._

## Released

Newest first. One file per version.

| Version | Date | Summary |
|---------|------|---------|
| [0.1.1](docs/CHANGELOG/v0.1.1.md) | 2026-09-08 | Core dump capture and read-out over USB CDC |
| [0.1.0](docs/CHANGELOG/v0.1.0.md) | 2026-09-06 | First scaffold, its CI and its memory bank. |

[Unreleased]: https://github.com/dtbao-embedded-dev/app-updater/compare/v0.1.1...main
[0.1.0]: https://github.com/dtbao-embedded-dev/app-updater/releases/tag/v0.1.0
[0.1.1]: https://github.com/dtbao-embedded-dev/app-updater/releases/tag/v0.1.1
