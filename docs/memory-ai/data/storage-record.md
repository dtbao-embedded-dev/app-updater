---
title: Persisted Storage Record
category: data
order: 2
purpose: The single NVS blob the updater persists, its field layout, and the invariants a reader must enforce.
status: active
updated: 2026-09-06
source: middleware/storage/include/storage.h:44-58, middleware/storage/src/storage.c:56-66, middleware/storage/src/storage.c:176-207
confidence: confirmed
keywords: storage_record_t, STORAGE_RECORD_VERSION, crc32, manifest_url, check_interval_ms, boot_fail_count, nvs blob
---

# Persisted Storage Record

> One versioned, CRC-protected blob in NVS holds everything the updater must remember across a reboot; anything that fails validation falls back to compiled-in defaults.

## Shape

Stored as a single NVS blob under a namespace + key chosen at init (defaults
`updater` / `record`).

| Field | Type | Meaning | Notes |
|-------|------|---------|-------|
| `version` | uint16 | Record layout version | **Always first.** Currently `1`. |
| `length` | uint16 | Size of the record as written | Lets newer firmware read a shorter old record. |
| `manifest_url` | char[128] | HTTPS manifest URL | NUL-terminated. Default points at an `.invalid` host. |
| `last_ok_fw_version` | char[32] | Last image confirmed healthy | Empty by default. |
| `check_interval_ms` | uint32 | Milliseconds between update checks | Default 6 h. `0` disables checking. |
| `boot_fail_count` | uint32 | Unconfirmed boots since the last good one | `0` by default. |
| `crc32` | uint32 | CRC-32 over every byte above it | **Always last.** |

## Invariants

1. `version` is the first field and `crc32` the last. The CRC covers exactly the
   bytes before `crc32`, computed as the offset of that member.
2. Every field has a compiled-in default. A device with erased flash must boot
   into a usable state, so no setting is left uninitialised "because it is
   always written".
3. The CRC is validated on **every** read. A power cut mid-write and flash
   bit-rot both produce a record that parses fine and means nothing.
4. `manifest_url` must be NUL-terminated within its buffer; the validator
   rejects a record whose last byte is not `\0`, because a stored string is
   untrusted input.
5. A record whose stored `length` differs from the compiled `sizeof` is
   rejected as damaged.
6. An unrecognised `version` falls back to defaults rather than being
   reinterpreted.

## Validation verdicts

`record_validate()` returns, in this order of checks:

| Condition | Verdict |
|-----------|---------|
| length mismatch | `FW_ERR_CRC` |
| CRC mismatch | `FW_ERR_CRC` |
| unknown `version` | `FW_ERR_NOT_FOUND` |
| `manifest_url` unterminated | `FW_ERR_CRC` |
| otherwise | `FW_OK` |

The caller treats `FW_ERR_CRC` and `FW_ERR_NOT_FOUND` the same way — use the
defaults and log it. The module never guesses on the caller's behalf.

## Formats & encoding

The blob is written and read as a raw struct, so its byte layout is whatever the
compiler produces for the target. That is acceptable because only this firmware
on this chip ever reads it. **It is not a wire format** — an OTA manifest or a
host tool must not assume this layout.

🔴 **Open hole (code, not knowledge):** there is no migration path yet. `STORAGE_RECORD_VERSION` is `1` and
`record_validate()` carries a TODO for the chained per-version migration
functions. Adding a field to this struct today invalidates every deployed
record; the deployed unit falls back to defaults, silently losing its
`manifest_url`. Resolve before the first field release.

## See also

- [../interface/storage-api.md](../interface/storage-api.md) — the contract that reads and writes it
- [../behavior/config-load-and-save.md](../behavior/config-load-and-save.md) — the load/save algorithm
