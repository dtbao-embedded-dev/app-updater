---
title: Storage API
category: interface
order: 3
purpose: The contract for loading and saving the persisted updater record in NVS.
status: active
updated: 2026-09-06
source: middleware/storage/include/storage.h, middleware/storage/src/storage.c:48-174
confidence: confirmed
keywords: storage.h, storage_init, storage_deinit, storage_record_load, storage_record_save, storage_cfg_default, storage_record_default, storage_t, storage_cfg_t
---

# Storage API

> Open a namespace, read a validated record or learn it is unusable, write one only when it actually changed.

## Contract

| Name | Signature (text) | Does | Returns / errors |
|------|------------------|------|------------------|
| `storage_cfg_default` | `storage_cfg_t storage_cfg_default(void)` | Namespace `updater`, key `record` | By value; every field set |
| `storage_record_default` | `storage_record_t storage_record_default(void)` | The compiled-in defaults, CRC already correct | By value |
| `storage_init` | `fw_err_t storage_init(storage_t *st, const storage_cfg_t *cfg)` | Opens the NVS namespace read-write | `FW_OK`; `FW_ERR_PARAM` on NULL; `FW_ERR_STATE` if already open; `FW_ERR_IO` if NVS refused |
| `storage_deinit` | `fw_err_t storage_deinit(storage_t *st)` | Closes the handle | `FW_OK`; `FW_ERR_PARAM` on NULL |
| `storage_record_load` | `fw_err_t storage_record_load(storage_t *st, storage_record_t *out_rec)` | Reads and validates | `FW_OK`; `FW_ERR_NOT_FOUND` if never written or version unknown; `FW_ERR_CRC` if damaged; `FW_ERR_IO`; `FW_ERR_PARAM`/`FW_ERR_STATE` |
| `storage_record_save` | `fw_err_t storage_record_save(storage_t *st, const storage_record_t *rec)` | Stamps version/length/CRC, then writes if changed | `FW_OK`; `FW_ERR_IO`; `FW_ERR_PARAM`/`FW_ERR_STATE` |

## Parameters & config

`storage_cfg_t` holds two `const char *` — `nvs_namespace` and `nvs_key`. They
are **borrowed, not copied**: the strings must outlive the instance. `storage_t`
keeps the key pointer so both load and save address the configured key, not a
compiled-in one.

`storage_t` also holds the vendor NVS handle as a `uint32_t`, so no SDK type
appears in this middleware header.

## Contract rules

- `nvs_flash_init()` must succeed **before** `storage_init()`. This module does
  not initialise the NVS subsystem; the application does.
- `storage_record_save()` overwrites the caller's `version`, `length` and
  `crc32` — a caller must not try to set them.
- `storage_record_save()` blocks on flash and is not callable from an ISR. One
  caller only.
- On `FW_ERR_CRC` or `FW_ERR_NOT_FOUND` the caller is expected to fall back to
  `storage_record_default()`. The module never substitutes defaults itself.

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

- [../data/storage-record.md](../data/storage-record.md) — the record shape and its invariants
- [../behavior/config-load-and-save.md](../behavior/config-load-and-save.md) — the algorithm behind these calls
