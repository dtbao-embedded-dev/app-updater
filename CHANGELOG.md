# Changelog

All notable changes to this project. The format is newest-first, one heading
per released version, and the version numbers follow SemVer (R-VER-01).

## [Unreleased]

### Added
- Repository skeleton in the R-RPO-09 layer tree: `application/app`,
  `application/updater`, `middleware/fw`, `middleware/ota_http`,
  `middleware/storage`, `driver/bsp`.
- Build entry for product 0xF001 (ESP32-S3, ESP-IDF 6.x) with two OTA slots,
  no factory partition, and app rollback enabled.
- Host tests for the update schedule and for the project-wide status code.
- `docs/scripts/tool-esp.py` for build, flash, monitor, size and format; a pre-commit
  hook that enforces `clang-format`.

[Unreleased]: https://github.com/dtbao-embedded-dev/app-updater/compare/v0.1.0...main
