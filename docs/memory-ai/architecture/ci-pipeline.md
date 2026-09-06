---
title: CI and Release Pipeline
category: architecture
order: 4
purpose: What GitHub Actions runs on a push and on a tag, in which container, and which gates can stop a release.
status: inferred
updated: 2026-09-06
source: .github/workflows/ci.yml, .github/workflows/release.yml
confidence: inferred
keywords: GitHub Actions, ci.yml, release.yml, espressif/idf:v6.1, ctest, clang-format, clang-tidy, cppcheck, gitleaks, ASan, UBSan, gh release create, SHA256SUMS
---

# CI and Release Pipeline

> Three checks on every push, and a tag that cannot publish unless it agrees with `VERSION` and has a changelog entry waiting for it.

🟡 **inferred:** neither workflow has ever executed. They are written against
the documented behaviour of the actions and of the `espressif/idf:v6.1` image
(confirmed to exist on Docker Hub), but the Docker daemon was unavailable when
they were authored. The first green run is the confirmation.

## CI — `.github/workflows/ci.yml`

Triggers on push to `main`, `developing`, `release/**`, and on pull requests
into `main` or `developing`. Concurrent runs of the same ref cancel each other.

| Job | Runs in | Does | Fails when |
|-----|---------|------|-----------|
| `format` | `ubuntu-latest` | `tool-esp.py format --check` | A source file is not `clang-format` clean |
| `analyse` | `espressif/idf:v6.1` | `cppcheck` + `clang-tidy` | Any finding in our code |
| `host-tests` | `espressif/idf:v6.1` | Configure `test/host`, build, `ctest` | A test fails, or our code trips the warning set |
| `sanitizers` | `espressif/idf:v6.1` | The same suite with `-DSANITIZE=ON` | ASan or UBSan trips (the build traps, it does not just report) |
| `firmware` | `espressif/idf:v6.1` | `idf.py build`, then `idf.py size` | A warning in our code, or a build error |
| `secrets` | `ubuntu-latest` | `gitleaks` over the **full history** | A credential is found anywhere in the tree |

All six run in parallel; a concurrent push to the same ref cancels the earlier
run.

**Every tool version is pinned**, named once in the workflow's `env` block:
`clang-format 22.1.5`, `clang-tidy 22.1.8`, `gitleaks 8.30.1`. An unpinned CI
rejects on Tuesday what it accepted on Monday, on code nobody touched — and a
CI that disagrees with the pre-commit hook is worse than no CI, because
developers learn to ignore it.

The host-test and sanitizer jobs need no extra install: Unity comes from the
ESP-IDF checkout already inside the image. The `analyse` job adds `cppcheck`
through apt and `clang-tidy` through pip.

`gitleaks` is installed from its pinned release tarball rather than through a
third-party action, so nothing else executes while a token is in scope.

## Release — `.github/workflows/release.yml`

Triggers on a tag matching `v*`. Two jobs, because the ESP-IDF image has the
toolchain and the plain runner has the `gh` CLI.

1. **`build`** (in `espressif/idf:v6.1`)
   - **Gate:** the tag with its `v` stripped must equal the `VERSION` file. A
     mismatch means one of the two is a typo, and shipping either is worse than
     shipping neither (R-VER-01).
   - Build, capture the size report.
   - Collect into `dist/`: the app `.bin` / `.elf` / `.map`, the bootloader, the
     partition table, the flash args, the size report, and a `SHA256SUMS` over
     all of them.
2. **`publish`** (on `ubuntu-latest`)
   - **Gate:** `docs/CHANGELOG/<tag>.md` must exist. It should already, because
     an entry is written the day the change is made (R-VER-05); if it does not,
     the release was never prepared and an unexplained binary is not a release.
   - `gh release create` with `--verify-tag`, the changelog file as the notes,
     and everything in `dist/` attached.

## Reproduction notes

- Both containers run `git config --global --add safe.directory` before
  anything else; a checkout owned by a different uid inside a container is
  otherwise refused by git.
- Every step that calls `idf.py` or `cmake` sources `$IDF_PATH/export.sh` first.
  A `container:` step does not inherit the image's entrypoint, so nothing is on
  `PATH` without it.
- The image tag is pinned to `v6.1`, not `release-v6.1`: the latter moves.
- `publish` needs `contents: write`; the token comes from `github.token`, so no
  secret is stored in the repo.

## See also

- [../rule/static-analysis.md](../rule/static-analysis.md) — what the analysers check and what is switched off
- [build-and-toolchain.md](build-and-toolchain.md) — what the build itself does
- [../rule/testing.md](../rule/testing.md) — the suite the `host-tests` job runs
- [../rule/versioning-and-release.md](../rule/versioning-and-release.md) — the procedure the tag gates enforce
