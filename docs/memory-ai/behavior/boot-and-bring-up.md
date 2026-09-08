---
title: Boot and Bring-Up
category: behavior
order: 1
purpose: What runs from app_main to the main loop, in what order, and what happens when a step fails.
status: active
updated: 2026-09-08
source: application/app/src/app.c:130-201, application/app/src/app.c:373-395
confidence: confirmed
keywords: app_main, app_run, print_banner, APP_GIT_COMMIT, bring_up_storage, bring_up_bsp, bring_up_updater, confirm_or_roll_back, report_last_panic, coredump_info_get, coredump_reason_get, on_updater_state, now_ms, APP_TICK_MS
---

# Boot and Bring-Up

> Decide the fate of the running image first, then bring every module up and check every status, and only then let the loop run.

## What it does

1. **Print the banner** (`print_banner`): project name, version, git commit,
   build date and time, IDF version, chip model and revision, feature bits,
   core count, CPU clock and the Wi-Fi station MAC. A unit that cannot say
   what it runs cannot be debugged. It uses `printf`, not `ESP_LOGI`, so it
   carries no level, tag or timestamp and no log-level setting can filter it
   away. See the note below on what it duplicates.
2. **Confirm or roll back**, before anything that could make the decision
   impossible. See below.
3. **Report the last panic** (`report_last_panic`): if the `coredump` partition
   holds anything, log the panic reason as text and say how to fetch the bytes.
   See below.
4. Zero the one application context struct that owns every module's instance.
5. Bring up **storage**: initialise NVS (erasing and retrying once if it is
   unusable), open the namespace, load the record. A missing or damaged record
   is not a failure — log it and use the compiled-in defaults.
6. Bring up **bsp**: resolve board revision 0 and claim its pins. Map the
   driver's status onto the project status.
7. Bring up **updater**: take the check interval from the loaded record, install
   the state callback, init and start with the current clock.
8. Loop forever: step the updater with `now_ms()`, log any non-OK step once,
   delay `APP_TICK_MS` (1000 ms).

Each bring-up step returns early on failure with the failure logged; nothing
starts until every module is up.

## Report the last panic

Third, and deliberately after the rollback decision: R-VER-08 gets the first
word, because nothing may run before the decision that a fresh image is or is
not confirmed. Everything else about this step is ordered for a person reading
a boot log top-down — the banner says what is running, then the log says why the
last run ended.

It needs **no module up**. `driver/coredump` has no lifecycle at all: it finds
the partition by subtype on every call, so this works before storage, bsp or USB
exist ([../interface/coredump-api.md](../interface/coredump-api.md)).

| Situation | What is logged |
|-----------|----------------|
| `coredump_info_get()` fails | Nothing. A unit whose coredump partition cannot be read has a table problem, and the boot that follows will show it |
| No dump stored | Nothing. A "no core dump" line on every clean boot trains a reader to skip the one boot where it matters |
| Dump stored, reason readable | `last boot ended in a panic: <reason>`, then `core dump valid|CORRUPT, <n> bytes - read it with tool-usb.py dump` |
| Dump stored, no reason in it | The same two lines with `reason unavailable`. Still worth announcing: it tells a reader to go fetch the bytes, which is more than silence does |

The reason is **already text** by the time this runs. The panic handler wrote
the dump before `panic_restart()`, and `esp_core_dump_get_panic_reason()` returns
a string, so nothing here has to symbolise an address — that is what makes this
a one-call step rather than a host-tool problem.

**It does not erase what it read.** The dump stays for `DUMP_READ` to fetch, and
with `CONFIG_ESP_COREDUMP_FLASH_NO_OVERWRITE` on, erasing here would throw away
the first crash of a boot loop — which is the one that explains it.

`report_last_panic` is `static` with a single call site, so under `-Os` it is
inlined and carries **no symbol in `app_updater.map`**. What proves it shipped is
its two log format strings in `app_updater.bin` plus the references to
`esp_core_dump_get_panic_reason` in the map; a map-symbol grep is the wrong check
for a static function, and only works for cross-unit ones like `cfg_init`.

## The banner overlaps ESP-IDF's own

ESP-IDF logs an *Application information* block of its own just before
`app_main()`, from `esp_app_desc.c` under tag `app_init`: project name, app
version, compile time, ELF SHA256 and IDF version. Four of those lines say the
same thing the banner says, so a boot log shows both.

What the banner adds that ESP-IDF's block does not have: the **git commit**,
the **chip model, revision and features**, the **core count and clock**, and
the **MAC**. Suppressing IDF's block is not free — the `CONFIG_APP_EXCLUDE_*`
options strip the fields from the image rather than only from the log, which
would empty the banner too.

`APP_GIT_COMMIT` is the full 40-character hash from `git rev-parse HEAD`, run by
`application/app/CMakeLists.txt` at **CMake configure time**, as a PRIVATE
compile definition so the vendor SDK never sees it. It therefore names the
commit the build tree was last configured on, not necessarily the one checked
out now, and falls back to `"unknown"` where git is unavailable.

## Confirm or roll back

Runs only when the bootloader marked the running partition `PENDING_VERIFY`,
which happens on the first boot after an OTA write:

| Situation | Action |
|-----------|--------|
| Cannot read the partition state | Return, do nothing |
| State is not `PENDING_VERIFY` | Return — this is an ordinary boot |
| State is `PENDING_VERIFY` | Confirm the image as valid, cancelling rollback |

🔴 **Open hole (code, not knowledge):** the confirmation is currently **unconditional** — there is no
self-test between "pending" and "confirmed". The source carries an explicit
`SPEC-DEVIATION` marker for it. An image that can confirm itself while broken
defeats the rollback the whole product exists to provide. This is the single
most important hole in the repo. See
[../rule/known-deviations.md](../rule/known-deviations.md).

## Inputs → outputs

| Reads | Produces |
|-------|----------|
| Image header (name, version, IDF version) | Boot log line |
| `otadata` partition state | Image confirmed valid, or left pending |
| NVS record, or defaults | The updater's check interval |
| Board table row 0 | Claimed LED pin |
| `esp_timer` monotonic microseconds | `now_ms()` for every step |

Side effect on every updater state change: the status LED is lit while the state
is `CHECKING` or `DOWNLOADING`, dark otherwise. A `BSP_ERR_NO_PIN` result there
is treated as success, so a board with no LED is not a warning storm.

## Edge cases & error handling

- **NVS unusable** (no free pages, or a newer NVS version): erase and re-init
  once. A second failure aborts bring-up with `FW_ERR_IO`.
- **A bring-up step fails:** `app_run()` returns the status. `app_main()` logs it
  and calls `abort()`. Stopping loudly beats running on with half a stack up, and
  asserts stay enabled in Release for the same reason.
- **A step fails inside the loop:** logged once, at this layer — the layer that
  decides what to do about it — and the loop continues. The cycle carries its own
  retry budget, so the app does not add a second one.
- `now_ms()` truncates a 64-bit microsecond counter to 32-bit milliseconds, so it
  wraps every ~49.7 days. That is expected; the cycle's scheduling handles it.

## See also

- [update-cycle-fsm.md](update-cycle-fsm.md) — what a step does
- [config-load-and-save.md](config-load-and-save.md) — the record load in step 4
