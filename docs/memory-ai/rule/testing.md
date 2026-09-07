---
title: Testing
category: rule
order: 3
purpose: Where a test lives, how to run it without a board, and how to tell a test that passes from a test that works.
status: active
updated: 2026-09-07
source: test/host/CMakeLists.txt, test/host/runner.c, test/host/stub/esp_log.h, middleware/fw/test/test_fw.c, application/updater/test/test_updater.c, .github/workflows/ci.yml
confidence: confirmed
keywords: Unity, ctest, test/host, runner.c, UNITY_DIR, host_tests, esp_log stub, mutation, fake, stub, ota_fake, bsp_fake, nvs_fake, driver fake, 83 tests
---

# Testing

> Pure logic is tested on a PC under the firmware's own warning set; a test that has never been seen to fail has not been shown to test anything.

## When this applies

Adding or changing any logic that does not need a register: a state machine
step, a parser, a conversion, a status mapping.

## The rule

1. **A module's tests live with the module**, at `<module>/test/test_<mod>.c`.
   They move with it when it is reused. Only the harness is shared, at
   `test/host/`.
2. Run them with `python docs/scripts/tool-esp.py test`. No board, no flashing.
3. **Add the new test function to `test/host/runner.c`.** Nothing discovers
   tests automatically — a function not listed there compiles, links, and never
   runs, and nothing goes red to tell you.
4. Also declare the function at the top of its own test file. It is a
   non-static symbol, so `-Wmissing-prototypes` requires it; the declaration
   list doubles as the file's index.
5. **Inject the clock.** A step function takes `now_ms` from its caller; nothing
   under test reads a system tick. That is what makes a 49.7-day wrap testable
   in microseconds.
6. Fake a vendor header at the module's own boundary, in `test/host/stub/`,
   never by patching the SDK. The logging fake keeps the `printf` format
   attribute so a `%lu` against an `int` still fails the build.
7. **Test the error paths.** The happy path is exercised by the product every
   second; the error paths are exercised once, in the field, at night.
8. The harness compiles our code with `-Werror` and the full firmware warning
   set. A host test must not be able to pass on code the target build rejects.
   It refuses to configure under MSVC for exactly that reason.
9. CI runs the suite **twice**: once plain, once with `-DSANITIZE=ON` for ASan
   and UBSan (R-SAN-08). The instrumented build traps on the first violation
   rather than reporting it, so undefined behaviour is a red run. Sanitizers
   are applied to our code only, never to Unity.
10. A new module's `test/` directory needs a copy of the `.clang-tidy` that
    already sits beside the existing tests — see
    [static-analysis.md](static-analysis.md).

## The harness must not inherit the cross-compile environment

Unity is found through `IDF_PATH`, and that is **all** the harness takes from
ESP-IDF. Handing it the full exported environment breaks it: that environment
puts `esp-clang` first on `PATH`, and CMake then tries to build the host tests
with a cross compiler for the target. `tool-esp.py test` therefore passes the
ambient environment plus `IDF_PATH`, nothing more.

`IDF_PATH` is also normalised with `file(TO_CMAKE_PATH ...)` before use. On
Windows it arrives with backslashes, and CMake reads `\.` and `` in a string
as escapes - `E:\.espressif6.1\esp-idf` is a parse error, not a path. Linux
never saw this, so CI stayed green while a Windows developer could not
configure.

## Prove the test fails

A green suite is evidence of nothing until you have seen it go red. Before
trusting a new test, break the code it covers and confirm that specific test
fails — then restore.

This is not ceremony. The wrap test in `test_updater.c` originally stepped the
clock only past the wrap point, where a correct implementation and a naive
`now_ms >= next_due_ms` give the *same* answer. It passed against both. Adding
one tick *before* the wrap — where `now_ms` is numerically larger than the
deadline but earlier than it — is what gave the test its teeth.

## Example

```text
python docs/scripts/tool-esp.py test        # 13 tests, 0 failures
```

Unity is taken from `$IDF_PATH`. To point it elsewhere, configure once:

```text
cmake -S test/host -B build/host -G Ninja -DUNITY_DIR=<dir containing unity.c>
```

## Not yet done

There are no on-target tests. `test/` holds only the host harness; an on-target
smoke test that boots, exercises every peripheral once and prints a verdict is
the last gate before a release tag, and it does not exist.

## `fake/` and `stub/` mean different things

`test/host/` has two directories and the difference is the whole point:

| Directory | Holds | Contents today |
|-----------|-------|----------------|
| `fake/` | Host implementations of **our own** driver headers | `ota_fake.c`, `bsp_fake.c`, `nvs_fake.c` |
| `stub/` | Shadows of **vendor** headers, first on the include path | `esp_log.h`, `esp_rom_crc.h`, `esp_err.h`, `nvs.h`, `nvs_flash.h` |

A test of middleware belongs in the first column. Before `driver/ota` and
`driver/bsp` existed, the tests for `middleware/command` had to shadow
`esp_ota_ops.h`, `esp_partition.h`, `esp_mac.h` and `esp_system.h` and pretend
to be ESP-IDF — nine stub files in all. Seven of them were deleted once
middleware stopped calling the SDK. What is left in `stub/` is there for
`driver/storage`, whose own tests have nothing below them to fake, plus the log
sink and one deliberate exception:

**`stub/esp_rom_crc.h` is not stubbing anything any more.** No firmware source
includes it. It is kept as an independent bitwise CRC-32 written straight from
the polynomial, so the protocol tests build their frames with one
implementation while the parser checks them with `fw_crc32_le()` — the two are
only interchangeable if they agree, and
`test_fw_crc32_agrees_with_an_independent_implementation` compares them
directly over 256 lengths.

Each fake carries a control surface (`<mod>_fake_reset`, forced failures,
observers) and **every test calls the reset first** (R-TST-05). The counts that
matter are observable: `ota_fake_open_sessions()` proves a session was freed
rather than leaked, and `nvs_fake_write_count()` proves the wear guard skipped
a redundant write.

The suite is **83 tests** as of 2026-09-07.

## See also

- [../architecture/ci-pipeline.md](../architecture/ci-pipeline.md) — where this suite runs automatically
- [../interface/tool-esp-cli.md](../interface/tool-esp-cli.md) — the command surface
