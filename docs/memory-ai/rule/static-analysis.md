---
title: Static Analysis
category: rule
order: 4
purpose: Which analysers run, what is switched off and why, and how to disposition a finding.
status: active
updated: 2026-09-06
source: .clang-tidy, application/updater/test/.clang-tidy, middleware/fw/test/.clang-tidy, .github/workflows/ci.yml, docs/scripts/tool-esp.py
confidence: confirmed
keywords: clang-tidy, cppcheck, gitleaks, ASan, UBSan, sanitizer, NOLINTNEXTLINE, SANITIZE, analyse
---

# Static Analysis

> The compiler first, then clang-tidy and cppcheck, then the host suite under ASan and UBSan; a suppression is a line with a reason, never a config-wide off switch.

## When this applies

Every push. Locally, before opening a pull request:

```text
python docs/scripts/tool-esp.py analyse
```

## What runs, and over what

| Tool | Where | Scope | Gate |
|------|-------|-------|------|
| Compiler warnings, `-Werror` | Every build, host and target | Our code only | Blocking |
| `clang-format --dry-run --Werror` | Pre-commit + CI | Our `.c` / `.h` | Blocking |
| `clang-tidy` | CI, and `analyse` locally | Files with a compile database | Blocking |
| `cppcheck --enable=warning,style` | CI, and `analyse` locally | Every `.c` we own | Blocking |
| ASan + UBSan | CI, on the host test build | Our code, not Unity | Blocking |
| `gitleaks` | CI, full history | Whole tree | Blocking |

**clang-tidy does not cover the whole tree, and pretending otherwise would be
worse than the gap.** It needs the exact compile flags, and the only build that
emits them for a compiler clang can parse is the host one — the firmware targets
Xtensa, which upstream clang does not support. So clang-tidy sees the
pure-logic modules; cppcheck, which needs no compile database, sees the rest.

## The rule

1. **Fix a finding rather than suppress it**, unless the tool is provably wrong.
   Most `bugprone-` hits on firmware are real: a narrowing conversion, a
   `sizeof` on a pointer, an uninitialised read on an error path.
2. A suppression is a `NOLINTNEXTLINE(<check>)` **at the line, with the
   reason** — never a check removed from the config to make a build green.
3. A check disabled in `.clang-tidy` carries its justification in that file.
   Read the comments there before adding a seventh.
4. **Never run the analysers over the vendor SDK.** Findings nobody can fix
   train everyone to ignore the report.
5. Test directories carry their own `.clang-tidy` that inherits the root config
   and drops exactly two checks — a test function is deliberately non-static so
   the runner can call it, and an out-of-range enum cast is the thing under
   test. A new module's `test/` needs a copy of that file.

## Versions are pinned

`clang-format 22.1.5`, `clang-tidy 22.1.8`, `gitleaks 8.30.1`, named once at the
top of `ci.yml`. An unpinned analyser rejects on Tuesday what it accepted on
Monday, on code nobody touched. Match `clang-tidy` locally with
`pip install clang-tidy==22.1.8`; the local `clang-format` must be 22.1.5
exactly, because formatting differences are diffs.

## What they find today, and why that is not the point

On the current tree all four are at **zero findings** — verified by running
them, not assumed. cppcheck found one real issue while this was being set up
(a parameter that could be `const`) and it was fixed rather than suppressed.

A 2000-line scaffold has little to find. The value is in the code that is not
written yet: the download path that writes flash, the manifest parser, the OTA
write. That is where pointer and boundary bugs live, and turning the analysers
on while the baseline is clean costs nothing — turning them on afterwards means
triaging fifty findings at once, which nobody does.

## Deferred: `gcc -fanalyzer`

**Decision: not enabled today, and this is the note that says when to enable
it.** Verified to run clean on the current tree, so turning it on later starts
from a green baseline just as the others did.

Why not now: it is not in the house standard, and what it is good at —
`malloc`/`free` pairing, use-after-free, double-free, leaked file descriptors,
null dereference along a specific path — has nothing to work on. This codebase
allocates nothing, opens nothing, and its two implemented modules are pure
logic. A check with no subject is a check that only produces false positives.

**Turn it on when any of these lands** — each gives it something real to find:

| Trigger | What it would catch |
|---------|---------------------|
| The OTA download path starts writing flash with offset/length arithmetic (`drain_body` into `esp_ota_write`) | A write past the end of the slot, a length that underflows |
| A manifest parser appears | Reading past a buffer on a malformed response |
| Any module starts calling `malloc`/`calloc`/`free` | Leak, double-free, use-after-free |
| Anything opens a file, socket or `esp_http_client` handle outside a single function | A handle leaked on the error path |

**How, when the time comes.** It is one flag on the host build, and the pattern
is already in place — add it beside the sanitizer block in
`test/host/CMakeLists.txt`, guarded by its own option so it can be introduced
without blocking anyone:

```text
option(ANALYZER "Build the host tests with -fanalyzer" OFF)
# ... target_compile_options(host_tests PRIVATE -fanalyzer)
```

then a job in `ci.yml` mirroring `sanitizers`. Introduce it **blocking from the
first commit**: a non-blocking analyser is one nobody reads. If the first run is
noisy, disable the individual checker with a reason — the same discipline as
every other suppression here (R-SAN-02) — rather than leaving the whole job
advisory.

## See also

- [../architecture/ci-pipeline.md](../architecture/ci-pipeline.md) — the jobs these run in
- [testing.md](testing.md) — the suite the sanitizers instrument
- [formatting-and-hooks.md](formatting-and-hooks.md) — the one check that also runs pre-commit
