# Changelog

All notable changes to this project, SemVer, single source of truth in
`VERSION` (R-VER-01).

An entry lands under `[Unreleased]` **the day the change is made**, not at
release time (R-VER-05). Releasing moves that whole section into its own file
under `docs/CHANGELOG/v<version>.md`, adds a row to the **Released** index
below, and leaves a fresh empty `[Unreleased]` here.

**Nothing is released yet.** There is no git tag and no `docs/CHANGELOG/`
directory; both appear with the first release.

## [Unreleased]

### Added
- Repository skeleton in the R-RPO-09 layer tree: `application/app`,
  `application/updater`, `middleware/fw`, `middleware/ota_http`,
  `middleware/storage`, `driver/bsp`.
- Build entry for product 0xF001 (ESP32-S3, ESP-IDF 6.x) with two app slots and
  app rollback enabled.
- Host tests for the update schedule and for the project-wide status code, with
  a CMake/CTest harness that runs them without a board.
- `docs/scripts/tool-esp.py` for build, flash, monitor, size, test and format;
  a pre-commit hook that enforces `clang-format`.
- Memory bank under `docs/memory-ai/`, wired into `CLAUDE.md`.
- GitHub Actions `ci.yml`, six jobs on every push: `clang-format`, `cppcheck` +
  `clang-tidy`, host tests, the same tests under ASan/UBSan, the firmware
  build, and a `gitleaks` scan of the full history. Every tool version pinned.
- GitHub Actions `release.yml`: builds in the ESP-IDF v6.1 image on a `v*` tag
  and publishes it, refusing any tag that disagrees with `VERSION` or has no
  changelog file waiting for it.
- `.clang-tidy` at the root with every disabled check justified, plus a
  narrower one in each test directory.

[Unreleased]: https://github.com/dtbao-embedded-dev/app-updater/commits/main
