# Changelog

All notable changes to this project, SemVer, single source of truth in
`VERSION` (R-VER-01).

An entry lands under `[Unreleased]` **the day the change is made**, not at
release time (R-VER-05). Releasing moves that whole section into its own file
under `docs/CHANGELOG/v<version>.md`, adds a row to the **Released** index
below, and leaves a fresh empty `[Unreleased]` here.

## [Unreleased]

### Fixed
- The published release artifacts could not flash a blank board. `flash_args`
  named `ota_data_initial.bin`, which was never uploaded, and referred to the
  bootloader and partition table by the directories they sit in inside
  `build/` while the assets are flat. Both are fixed, and the release workflow
  now fails if either recurs. **v0.1.0 is affected**; use the individual
  offsets from its release notes, or the factory image from the next tag.

### Added
- Releases now carry a merged `app-updater-<tag>-factory.bin` that provisions a
  blank board with one command at `0x0`, plus `ota_data_initial.bin` and the
  `sdkconfig` that produced the image.

## Released

Newest first. One file per version.

| Version | Date | Summary |
|---------|------|---------|
| [0.1.0](docs/CHANGELOG/v0.1.0.md) | 2026-09-06 | First scaffold, its CI and its memory bank. |

[Unreleased]: https://github.com/dtbao-embedded-dev/app-updater/compare/v0.1.0...main
[0.1.0]: https://github.com/dtbao-embedded-dev/app-updater/releases/tag/v0.1.0
