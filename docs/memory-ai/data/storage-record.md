---
title: Persisted Settings Record
category: data
order: 2
purpose: The single versioned, CRC-protected record the device persists, its field layout, and the invariants a reader must enforce.
status: active
updated: 2026-09-07
source: middleware/cfg/include/cfg.h:88-113, middleware/cfg/src/cfg.c:44-54, middleware/cfg/src/cfg.c:238-290
confidence: confirmed
keywords: cfg_record_t, CFG_RECORD_VERSION, crc32, manifest_url, last_ok_fw_version, check_interval_ms, boot_fail_count, record_validate, nvs blob, settings record
---

# Persisted Settings Record

> One versioned, CRC-protected record holds everything the device must remember across a reboot; anything that fails validation falls back to compiled-in defaults.

**The record is `cfg_record_t`, owned by `middleware/cfg`.** Where it is stored
is a separate decision: the shipped adapter hands the bytes to
`driver/storage`, which writes them as one opaque NVS blob under a
namespace + key chosen at init (defaults `updater` / `record`). Nothing in the
layout below depends on that choice.

## Shape

| Field | Type | Meaning | Notes |
|-------|------|---------|-------|
| `version` | uint16 | Record layout version | **Always first.** Currently `1`. |
| `length` | uint16 | Size of the record as written | Lets newer firmware recognise a shorter old record. |
| `manifest_url` | char[128] | HTTPS manifest URL | NUL-terminated. Default points at an `.invalid` host. |
| `last_ok_fw_version` | char[32] | Last image confirmed healthy | Empty by default. |
| `check_interval_ms` | uint32 | Milliseconds between update checks | Default 6 h. `0` disables checking. Must be `< 0x80000000`. |
| `boot_fail_count` | uint32 | Unconfirmed boots since the last good one | `0` by default. |
| `crc32` | uint32 | CRC-32 over every byte above it | **Always last.** |

Total 176 bytes on the target: every field is naturally aligned in this order,
so the struct carries no padding and the CRC covers no indeterminate bytes.

## Invariants

1. `version` is the first field and `crc32` the last. The CRC covers exactly the
   bytes before `crc32`, computed as the offset of that member.
2. Every field has a compiled-in default. A device with erased storage must come
   up in a usable state, so no setting is left uninitialised "because it is
   always written".
3. The CRC is validated on **every** read. A power cut mid-write and flash
   bit-rot both produce a record that parses fine and means nothing.
4. **Both** strings must be NUL-terminated within their own buffer. A getter
   reads to the first NUL, so an unterminated field is a read past the end of
   it — and a stored string is untrusted input.
5. A record whose stored `length` differs from the compiled `sizeof` is
   rejected as damaged.
6. An unrecognised `version` falls back to defaults rather than being
   reinterpreted.
7. **A valid CRC is not enough.** `check_interval_ms` must be below the update
   cycle's scheduling horizon; a record that passes its CRC and holds a larger
   value is refused whole. Refusing beats clamping: the defaults are known
   good, a clamped value is a setting nobody chose.
8. A string setter zeroes the entire field before copying. Those bytes are in
   the CRC, so a stale tail would make two records holding identical settings
   compare unequal and cost a flash write the adapter would otherwise skip.

## Validation verdicts

`record_validate()` in `middleware/cfg/src/cfg.c` returns, in this order of
checks:

| Condition | Verdict |
|-----------|---------|
| length mismatch | `FW_ERR_CRC` |
| CRC mismatch | `FW_ERR_CRC` |
| unknown `version` | `FW_ERR_NOT_FOUND` |
| either string unterminated | `FW_ERR_CRC` |
| `check_interval_ms >= CFG_CHECK_INTERVAL_MAX_MS` | `FW_ERR_PARAM` |
| otherwise | `FW_OK` |

`cfg_init()` treats all three failure verdicts the same way — use the defaults,
log which one it was, and still report `FW_OK` to the caller, because the
instance is usable. The module never guesses which half of a damaged record
survived.

## Formats & encoding

The record is written and read as a raw struct, so its byte layout is whatever
the compiler produces for the target. That is acceptable because only this
firmware on this chip ever reads it. **It is not a wire format** — an OTA
manifest or a host tool must not assume this layout.

🔴 **Open hole (code, not knowledge):** there is no migration path yet.
`CFG_RECORD_VERSION` is `1` and `record_validate()` carries a TODO for the
chained per-version migration functions. Adding a field to this struct today
invalidates every deployed record; the deployed unit falls back to defaults,
silently losing its `manifest_url`. Resolve before the first field release.

## See also

- [../interface/cfg-api.md](../interface/cfg-api.md) — the accessors that read and write it, and what each refuses
- [../interface/storage-api.md](../interface/storage-api.md) — the NVS blob store the shipped adapter calls
- [../behavior/config-load-and-save.md](../behavior/config-load-and-save.md) — the load/save algorithm
