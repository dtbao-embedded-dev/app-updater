---
title: Project Status API (fw)
category: interface
order: 1
purpose: The contract of the project-wide status type and its name lookup.
status: active
updated: 2026-09-07
source: middleware/fw/include/fw.h, middleware/fw/src/fw.c:25-56
confidence: confirmed
keywords: fw.h, fw_config.h, fw_err_t, fw_err_str, fw_crc32_le, FW_OK, FW_ERR_IO, fw_err_t codes, FW_FEATURE_USB_COMMAND, FW_FEATURE_UPDATER, feature switch, crc32
---

# Project Status API (fw)

> One enum and one lookup function; no state, no lifecycle, nothing to claim or release.

## Contract

| Name | Signature (text) | Does | Returns / errors |
|------|------------------|------|------------------|
| `fw_err_str` | `const char *fw_err_str(fw_err_t err)` | Maps a status to its constant name | A string literal with static lifetime. Never NULL, never freed by the caller. An unrecognised value yields `"FW_ERR_UNKNOWN"`. |


## Parameters & config

None. This module takes no configuration and holds no instance.

## Contract rules

- `fw_err_str()` is reentrant and callable from any task. It must not be called
  from an ISR, because its only purpose is to feed a log line and logging from
  an ISR is forbidden.
- The returned pointer is a literal in flash; the caller must not free, modify,
  or assume it is unique per call.
- This module has **no** `init` / `deinit`. The five lifecycle verbs apply to
  modules that claim something; this one claims nothing.

## Reproduction note

A rebuild must keep the switch in `fw_err_str()` free of a `default` label, so
the compiler's `-Wswitch-enum` fails the build when a code is added without a
name. The unknown-value fallback lives **after** the switch, not inside it.

## `fw_crc32_le()` — the project's CRC-32

```c
uint32_t fw_crc32_le(uint32_t seed, const void *data, size_t len);
```

Reflected CRC-32, polynomial `0xEDB88320`, init and xorout `0xFFFFFFFF` — the
zlib and Ethernet CRC, and bit for bit what `esp_rom_crc32_le()` computed for
this firmware before it. `seed` is 0 to start or a previous return value to
continue; `data` may be NULL only when `len` is 0.

**Chaining is exact**: `f(f(0,a,n),b,m)` equals `f(0,ab,n+m)`. That is what
lets the upgrade session fold a 13.875 MB image chunk by chunk instead of
re-reading the slot at the end, and it is asserted directly by
`test_fw_crc32_chains_across_two_buffers`.

It lives here, in the dependency-free leaf, rather than behind a driver,
because it is a pure function with no hardware under it — but it may not be a
vendor call either: the values it produces are compared against records and
frames that outlive any one chip. A 16-entry nibble table costs 64 bytes of
flash and buys two lookups per byte instead of eight shifts, which matters
because every byte of an incoming image passes through it.

Three tests pin it: the known-answer vector `CRC32("123456789") == 0xCBF43926`,
the chaining identity, and a direct comparison against the deliberately
independent bitwise implementation in `test/host/stub/esp_rom_crc.h` over 256
lengths. A single wrong table entry reddens the comparison and every protocol
frame test with it — proved by mutation, not assumed.

## `fw_config.h` — the compile-time feature switches

Not a status contract, but it ships in the same module because every component
already requires `fw`:

| Macro | Default | Off means |
|-------|---------|-----------|
| `FW_FEATURE_USB_COMMAND` | `1` | No CDC-ACM channel, no frame codec, no dispatcher. Frees 67 688 bytes of `.bss` (20.66 % of DRAM to 0.85 %) and 41.4 KB of flash, measured. |
| `FW_FEATURE_UPDATER` | `1` | No scheduled HTTP update cycle. `updater_step` ends up with no address in `app_updater.map` at all. |

Every `#if` on these lives at the wiring points in
`application/app/src/app.c` — a module never tests its own switch. A second
product's answers belong in a header under `workspace/<pid>/`, not in an
`#ifdef` on the product id here. See
[../architecture/build-and-toolchain.md](../architecture/build-and-toolchain.md).

## See also

- [../data/error-code-model.md](../data/error-code-model.md) — the values and their meanings
