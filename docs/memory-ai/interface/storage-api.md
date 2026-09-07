---
title: Storage API
category: interface
order: 3
purpose: The contract for putting one opaque blob in NVS and reading it back, with no opinion about what is in it.
status: active
updated: 2026-09-07
source: middleware/storage/include/storage.h, middleware/storage/src/storage.c:40-151
confidence: confirmed
keywords: storage.h, storage_init, storage_deinit, storage_blob_load, storage_blob_save, storage_cfg_default, storage_t, storage_cfg_t, STORAGE_BLOB_MAX, nvs_get_blob, nvs_set_blob
---

# Storage API

> Open a namespace, read the stored bytes back, write them only when they actually changed — and never look inside them.

## Responsibility

This module is a byte pusher. It knows a namespace, a key, and how to avoid
wearing a sector out. It does **not** know the record's fields, its version, or
its CRC — those belong to `middleware/cfg`, which owns the shape and hands
these two calls a finished blob.

The split is what makes the settings re-pointable: a second adapter writing the
reserved `cfg_setting` partition instead needs nothing from here and nothing
from `cfg`.

## Contract

| Name | Signature (text) | Does | Returns / errors |
|------|------------------|------|------------------|
| `storage_cfg_default` | `storage_cfg_t storage_cfg_default(void)` | Namespace `updater`, key `record` | By value; every field set |
| `storage_init` | `fw_err_t storage_init(storage_t *st, const storage_cfg_t *cfg)` | Opens the NVS namespace read-write | `FW_OK`; `FW_ERR_PARAM` on NULL; `FW_ERR_STATE` if already open; `FW_ERR_IO` if NVS refused |
| `storage_deinit` | `fw_err_t storage_deinit(storage_t *st)` | Closes the handle | `FW_OK`; `FW_ERR_PARAM` on NULL |
| `storage_blob_load` | `fw_err_t storage_blob_load(storage_t *st, void *out, size_t cap, size_t *out_len)` | Reads the stored bytes | `FW_OK`; `FW_ERR_NOT_FOUND` if never written; `FW_ERR_CRC` if the stored blob does not fit `cap`; `FW_ERR_NO_SPACE` if `cap > STORAGE_BLOB_MAX`; `FW_ERR_IO`; `FW_ERR_PARAM`/`FW_ERR_STATE` |
| `storage_blob_save` | `fw_err_t storage_blob_save(storage_t *st, const void *data, size_t len)` | Writes, but only if the bytes differ | `FW_OK`; `FW_ERR_NO_SPACE` if `len > STORAGE_BLOB_MAX` or NVS is full; `FW_ERR_IO`; `FW_ERR_PARAM`/`FW_ERR_STATE` |

The two blob signatures are deliberately one `storage_t *` away from
`cfg_load_cb_t` and `cfg_save_cb_t`, so the shipped adapter in
`application/app/src/app.c` is two one-line wrappers and nothing else.

## Parameters & config

`storage_cfg_t` holds two `const char *` — `nvs_namespace` and `nvs_key`. They
are **borrowed, not copied**: the strings must outlive the instance. `storage_t`
keeps the key pointer so both load and save address the configured key, not a
compiled-in one.

`storage_t` also holds the vendor NVS handle as a `uint32_t`, so no SDK type
appears in this middleware header.

`STORAGE_BLOB_MAX` is `256`. It exists because `storage_blob_save()` compares
the new bytes against the stored ones before writing, and that compare needs a
buffer whose size is known at compile time — a VLA on a task stack is how a deep
call chain overflows one. The settings record needs 176 bytes today.

## Contract rules

- `nvs_flash_init()` must succeed **before** `storage_init()`. This module does
  not initialise the NVS subsystem; the application does.
- **`FW_ERR_CRC` from `storage_blob_load()` is not a checksum verdict** — this
  module computes no checksum. It is what a stored blob of the wrong length
  comes back as: NVS refuses to copy a blob bigger than `cap`, so there is
  nothing else to report, and "damaged" is the verdict that makes the caller
  fall back to its defaults rather than fail bring-up on a record it could
  never have parsed. The real CRC check lives in `middleware/cfg`.
- Both blob calls block on flash and are not callable from an ISR. One caller
  only.
- Nothing here substitutes a default. `FW_ERR_NOT_FOUND` and `FW_ERR_CRC` go
  back to the caller, which decides (see `cfg_init()`).
- The instance pointer is always the first argument, the config struct always
  the second, both `const` where possible.
- The status is the return value; results leave through trailing
  out-parameters.
- `deinit` is safe to call twice and safe on a partly initialised instance —
  cleanup runs on the error path, where the instance is by definition
  half-built.
- An operation called in the wrong lifecycle state returns `FW_ERR_STATE`, never
  undefined behaviour. The instance carries its own state flag and operations
  check it first.
- Arguments are validated at the top of every public function, before any state
  changes. Private helpers assume that check already happened.

## Reproduction notes

- ⚠ **No host test covers this module**, and after the blob refactor there is
  nothing pure left in it to cover: every line is `nvs_open`, `nvs_get_blob`,
  `nvs_set_blob`, `nvs_commit`, the memcmp guard, or the `from_esp_err()` map.
  The logic that used to be testable here — defaults, version, CRC, string
  termination — moved to `middleware/cfg`, which has 13 of them.
- `PRIV_REQUIRES` no longer lists `esp_rom`: the only user of
  `esp_rom_crc32_le()` was the record CRC, and that left with the record.

## See also

- [cfg-api.md](cfg-api.md) — the module that owns the record and calls these two through an adapter
- [../data/storage-record.md](../data/storage-record.md) — the record shape and its invariants
- [../behavior/config-load-and-save.md](../behavior/config-load-and-save.md) — the algorithm behind these calls
