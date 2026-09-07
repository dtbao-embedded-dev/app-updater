---
title: Cfg API
category: interface
order: 12
purpose: The contract for reading and writing the device's settings, one validated accessor pair per setting, with persistence supplied by the caller as an adapter.
status: active
updated: 2026-09-07
source: middleware/cfg/include/cfg.h, middleware/cfg/src/cfg.c, middleware/cfg/test/test_cfg.c
confidence: confirmed
keywords: cfg.h, cfg_t, cfg_store_t, cfg_record_t, cfg_load_cb_t, cfg_save_cb_t, cfg_init, cfg_deinit, cfg_save, cfg_defaults_set, cfg_record_default, cfg_manifest_url_get, cfg_manifest_url_set, cfg_last_ok_fw_version_get, cfg_last_ok_fw_version_set, cfg_check_interval_ms_get, cfg_check_interval_ms_set, cfg_boot_fail_count_get, cfg_boot_fail_count_set, CFG_URL_MAX, CFG_VERSION_MAX, CFG_RECORD_VERSION, CFG_CHECK_INTERVAL_MAX_MS
---

# Cfg API

> `middleware/cfg` owns what a setting may be; it does not own where the setting goes — the caller hands it a load/save adapter and the module never names a storage technology.

## Responsibility

Two jobs used to live in `middleware/storage`: defining the settings record and
putting it in NVS. This module took the first. It holds the record, the
compiled-in defaults, and the validation that decides whether a value is
acceptable at all. Persistence leaves through `cfg_store_t`, the same
callback-with-`ctx` shape `ota_http` uses to report progress upward without
naming its listener.

That seam is the point: re-pointing the settings at the reserved `cfg_setting`
partition instead of NVS is a new adapter and not one edit inside this module.

## Constants

| Macro | Value | Meaning |
|-------|-------|---------|
| `CFG_RECORD_VERSION` | `1` | Record layout version, stamped on every save. |
| `CFG_URL_MAX` | `128` | `manifest_url` buffer, including its NUL. |
| `CFG_VERSION_MAX` | `32` | `last_ok_fw_version` buffer, including its NUL. |
| `CFG_CHECK_INTERVAL_MAX_MS` | `0x80000000` | Exclusive ceiling on `check_interval_ms`. |

`CFG_CHECK_INTERVAL_MAX_MS` is a **deliberate duplicate** of `UPDATER_HORIZON_MS`
in `application/updater/src/updater.c`. The update cycle schedules by comparing
a wrapped `uint32` difference against half its range, so an interval at or past
that reads as "already due" on every step. `middleware/` may not include a
header from `application/` (R-LAY-01), so the number is restated here and
validated where the value arrives — instead of failing bring-up minutes later.
Change one and you must change the other; nothing goes red to say so.

## Types

| Type | Role |
|------|------|
| `cfg_load_cb_t` | `fw_err_t (*)(void *ctx, void *out, size_t cap, size_t *out_len)` — reads the stored record back. |
| `cfg_save_cb_t` | `fw_err_t (*)(void *ctx, const void *data, size_t len)` — persists the whole record. |
| `cfg_store_t` | `{ load, save, ctx }`. Both callbacks required; `ctx` is borrowed and must outlive the instance. |
| `cfg_record_t` | The record itself — see [../data/storage-record.md](../data/storage-record.md). |
| `cfg_t` | `{ is_init, store, rec }`. Caller-allocated; the module owns the contents. |

`cfg_record_t` sits inside the public `cfg_t` only because the caller allocates
the instance, exactly as `command_upgrade_t` does inside `command_t`. Read and
write it through the accessors — they are the only things that validate.

## Contract

| Function | Returns |
|----------|---------|
| `cfg_record_default(void)` | The compiled-in record, by value, CRC already correct. No instance needed. |
| `cfg_init(c, store)` | `FW_OK` whenever the instance is usable — **including** when the stored record was missing, damaged or out of range and the defaults were taken instead. `FW_ERR_PARAM` on a NULL argument or a NULL callback, `FW_ERR_STATE` when already initialized, and any other failure the adapter's `load` reported, unchanged. |
| `cfg_deinit(c)` | `FW_OK`, or `FW_ERR_PARAM` on NULL. Repeatable and safe on a partly built instance (R-LFC-04); nothing is owned, so zeroing is the whole release. |
| `cfg_defaults_set(c)` | `FW_OK`, `FW_ERR_PARAM`, `FW_ERR_STATE`. **RAM only** — a factory reset that reboots before `cfg_save()` leaves the old record readable. |
| `cfg_save(c)` | `FW_OK`, `FW_ERR_PARAM`, `FW_ERR_STATE`, or whatever `save` returned. Stamps `version`, `length` and `crc32` first. |
| `cfg_<field>_get(c, out[, cap])` | `FW_OK`, `FW_ERR_PARAM` on NULL, `FW_ERR_STATE` before init. String getters add `FW_ERR_NO_SPACE` when `cap` cannot hold the string and its NUL — and copy nothing in that case. |
| `cfg_<field>_set(c, value)` | `FW_OK`, `FW_ERR_STATE` before init, `FW_ERR_PARAM` for any value this build cannot accept — **the stored value survives a refusal**. |

The four settings, with what each setter refuses:

| Setting | Accessor pair | Refused |
|---------|---------------|---------|
| `manifest_url` | `cfg_manifest_url_get` / `_set` | NULL, `""`, or a string needing `CFG_URL_MAX` bytes or more. Empty is refused because there is no such manifest — disabling checks is what a zero interval is for. |
| `last_ok_fw_version` | `cfg_last_ok_fw_version_get` / `_set` | NULL, or a string needing `CFG_VERSION_MAX` bytes or more. `""` is legal and is the shipped default: no image has confirmed itself yet. |
| `check_interval_ms` | `cfg_check_interval_ms_get` / `_set` | `>= CFG_CHECK_INTERVAL_MAX_MS`. `0` is accepted and means checking is disabled — a real setting, not a missing one (R-CFG-03). |
| `boot_fail_count` | `cfg_boot_fail_count_get` / `_set` | Nothing; every `uint32` is a legal count. |

## Lifecycle and threading

`cfg_init()` is the only thing that calls `load`, and `cfg_save()` the only
thing that calls `save` — **a setter never writes**. That split is what lets a
caller change four settings and pay for one flash write; `test_cfg.c` asserts
the negative with a call-counting fake store, so a setter that starts writing
goes red.

Both `cfg_init()` and `cfg_save()` block for as long as the adapter does and
have one caller only; neither is callable from an ISR. Getters are readable
from another task, with the usual caveat that the value may be stale on return.

## Reproduction notes

- No `esp_*` type appears in `cfg.h` (R-LAY-03); the whole surface is
  `fw_err_t`, `uint32_t`, `char *` and `size_t`. That is what makes the module
  compile and run in the host suite with no stub but `esp_log.h` and
  `esp_rom_crc.h`.
- `cfg_init()` sorts the adapter's return into two groups. `FW_ERR_NOT_FOUND`
  and `FW_ERR_CRC` are normal — nothing stored, or bytes that did not check out
  — and lead to the defaults. Everything else is the caller's problem, logged
  once here and returned unchanged (R-LOG-04).
- A string setter zeroes the whole field before copying, not just the tail past
  the new string. Those bytes go into the CRC, so a stale tail would make two
  records holding identical settings compare unequal and cost a flash write the
  adapter would otherwise skip.
- 13 host tests, all green, listed in `test/host/runner.c`. One was proved to
  have teeth by mutation: deleting the `CFG_CHECK_INTERVAL_MAX_MS` guard from
  the setter reddens exactly `test_cfg_check_interval_refuses_the_scheduling_horizon`
  and nothing else.

## See also

- [../data/storage-record.md](../data/storage-record.md) — the record's field layout and invariants
- [storage-api.md](storage-api.md) — the NVS blob store this module's shipped adapter calls
- [../behavior/config-load-and-save.md](../behavior/config-load-and-save.md) — the load/save algorithm end to end
- [updater-api.md](updater-api.md) — the consumer of `check_interval_ms`, and the owner of the horizon this module restates
