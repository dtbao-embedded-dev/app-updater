---
title: Updater API
category: interface
order: 5
purpose: The contract of the update cycle - lifecycle, the stepped state machine, and the state callback.
status: active
updated: 2026-09-06
source: application/updater/include/updater.h, application/updater/src/updater.c:45-178
confidence: confirmed
keywords: updater.h, updater_init, updater_start, updater_stop, updater_deinit, updater_step, updater_state_get, updater_state_str, updater_cfg_default, updater_state_t, updater_state_cb_t, UPDATER_FAIL_LIMIT
---

# Updater API

> Five lifecycle verbs plus a `step()` the caller drives with its own clock, which is what makes the whole cycle testable on a host.

## Contract

| Name | Signature (text) | Does | Returns / errors |
|------|------------------|------|------------------|
| `updater_state_str` | `const char *updater_state_str(updater_state_t state)` | Names a state | Static literal; `"UPDATER_STATE_UNKNOWN"` for an unrecognised value |
| `updater_cfg_default` | `updater_cfg_t updater_cfg_default(void)` | 6 h check interval, 60 s retry backoff, no callback | By value |
| `updater_init` | `fw_err_t updater_init(updater_t *up, const updater_cfg_t *cfg)` | Validates the config, state becomes IDLE. No traffic. | `FW_OK`; `FW_ERR_PARAM` on NULL **or an interval at/past 2^31 ms**; `FW_ERR_STATE` if already initialised |
| `updater_start` | `fw_err_t updater_start(updater_t *up, uint32_t now_ms)` | Arms the cycle, first check one interval out | `FW_OK`; `FW_ERR_PARAM`; `FW_ERR_STATE` before init or when already running |
| `updater_stop` | `fw_err_t updater_stop(updater_t *up)` | Disarms, returns to IDLE | `FW_OK`; `FW_ERR_PARAM` on NULL |
| `updater_deinit` | `fw_err_t updater_deinit(updater_t *up)` | Zeroes the instance | `FW_OK`; `FW_ERR_PARAM` on NULL |
| `updater_step` | `fw_err_t updater_step(updater_t *up, uint32_t now_ms)` | Advances the cycle by one step | `FW_OK`; `FW_ERR_PARAM` on NULL; `FW_ERR_STATE` before `start`; or the failure the step hit |
| `updater_state_get` | `fw_err_t updater_state_get(const updater_t *up, updater_state_t *out_state)` | Current state | `FW_OK`; `FW_ERR_PARAM`/`FW_ERR_STATE` |

## States

`UPDATER_STATE_IDLE` (0), `_CHECKING`, `_DOWNLOADING`, `_PENDING_BOOT`,
`_FAILED`. The transitions are in
[../behavior/update-cycle-fsm.md](../behavior/update-cycle-fsm.md).

## Parameters & config

| Field | Type | Range | Meaning |
|-------|------|-------|---------|
| `check_interval_ms` | uint32 | 0 .. 2^31-1 | Between checks. **`0` disables checking entirely** — a real setting, not a missing one. |
| `retry_backoff_ms` | uint32 | 0 .. 2^31-1 | Delay after a failed attempt |
| `on_state` | `void (*)(void *ctx, updater_state_t state)` | optional | Called after **every** state change |
| `on_state_ctx` | `void *` | — | Passed back unchanged |

The 2^31 ms (~24.8 day) ceiling is a real contract, not a formality: the due
check compares a wrapped difference against half the `uint32` range, so a longer
interval would read as already due on the first step. `updater_init()` rejects
it rather than silently checking every loop.

## Contract rules

- `now_ms` is supplied by the caller, must be monotonic, and must never go
  backwards. Wrap-around at 2^32 is handled and tested.
- `on_state` runs in the task that called `updater_step()`, never in an ISR, and
  **must not call back into the updater**.
- `updater_stop()` does not abort a download already in flight; it stops the
  next one starting.
- `updater_step()` blocks for as long as the work it starts. One caller only.

## Contract rules

- The instance pointer is always the first argument, the config struct always
  the second, both `const` where possible.
- Configuration arrives as one `const <mod>_cfg_t *`, never a long argument
  list, so a field can be added without breaking a caller.
- The status is the return value; results leave through trailing
  out-parameters.
- `deinit` (and `stop`, where present) is safe to call twice and safe on a
  partly initialised instance — cleanup runs on the error path, where the
  instance is by definition half-built.
- An operation called in the wrong lifecycle state returns `FW_ERR_STATE`, never
  undefined behaviour. The instance carries its own state flag and operations
  check it first.
- Arguments are validated at the top of every public function, before any state
  changes. Private helpers assume that check already happened.

## See also

- [../behavior/update-cycle-fsm.md](../behavior/update-cycle-fsm.md) — the state machine and the wrap arithmetic
