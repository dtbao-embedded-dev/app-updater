---
title: OTA HTTP API
category: interface
order: 4
purpose: The contract for downloading a firmware image over HTTPS and receiving it chunk by chunk.
status: active
updated: 2026-09-06
source: middleware/ota_http/include/ota_http.h, middleware/ota_http/src/ota_http.c:42-155
confidence: confirmed
keywords: ota_http.h, ota_http_init, ota_http_deinit, ota_http_fetch, ota_http_progress_get, ota_http_cfg_default, ota_http_chunk_cb_t, OTA_HTTP_CHUNK_BYTES
---

# OTA HTTP API

> The module owns the transport and nothing else: it never writes flash, it hands each chunk to a callback the caller supplied.

## Contract

| Name | Signature (text) | Does | Returns / errors |
|------|------------------|------|------------------|
| `ota_http_cfg_default` | `ota_http_cfg_t ota_http_cfg_default(void)` | Defaults with a 10 s timeout; `url`, `on_chunk`, `chunk_ctx` left NULL | By value |
| `ota_http_init` | `fw_err_t ota_http_init(ota_http_t *cli, const ota_http_cfg_t *cfg)` | Builds the HTTPS client. No traffic. | `FW_OK`; `FW_ERR_PARAM` on NULL url/callback or a zero timeout; `FW_ERR_STATE` if already initialised; `FW_ERR_NO_MEM` |
| `ota_http_deinit` | `fw_err_t ota_http_deinit(ota_http_t *cli)` | Cleans up the client | `FW_OK`; `FW_ERR_PARAM` on NULL |
| `ota_http_fetch` | `fw_err_t ota_http_fetch(ota_http_t *cli)` | Downloads the whole body, calling `on_chunk` in order | `FW_OK`; `FW_ERR_STATE` before init or while a fetch runs; `FW_ERR_TIMEOUT` on a truncated body; `FW_ERR_IO` on transport or non-200 status; or whatever `on_chunk` returned |
| `ota_http_progress_get` | `fw_err_t ota_http_progress_get(const ota_http_t *cli, uint32_t *out_bytes)` | Bytes delivered to the callback so far | `FW_OK`; `FW_ERR_PARAM`/`FW_ERR_STATE` |

## The chunk callback

`typedef fw_err_t (*ota_http_chunk_cb_t)(void *ctx, const uint8_t *data, size_t len)`

| Aspect | Contract |
|--------|----------|
| `ctx` | The `chunk_ctx` supplied at init, passed back unchanged |
| `data` | Valid **only** for the duration of the call — copy or consume before returning |
| `len` | 1 .. `OTA_HTTP_CHUNK_BYTES` (1024) |
| return | `FW_OK` continues; any failure aborts the fetch and is returned unchanged by `ota_http_fetch()` |
| context | Runs in the task that called `ota_http_fetch()`, never in an ISR. It may block, and blocking stalls the download. |

## Parameters & config

| Field | Type | Meaning |
|-------|------|---------|
| `url` | `const char *` | HTTPS image URL. Borrowed, must outlive the instance. |
| `cert_pem` | `const char *` | Server root cert, PEM. NULL uses whatever the build has compiled in. |
| `on_chunk` / `chunk_ctx` | callback + `void *` | Required; init rejects a NULL callback |
| `timeout_ms` | uint32 | Per-read budget. **`0` is rejected** — it would mean non-blocking, which this transport cannot honour. |

`ota_http_t` holds the vendor client handle as `void *`, so no SDK type appears
in this middleware header.

## Contract rules

- `ota_http_fetch()` blocks the calling task for the whole download. One caller
  only; not callable from an ISR.
- A second `fetch` on a running instance returns `FW_ERR_STATE` rather than
  interleaving.
- `ota_http_deinit()` is not safe while a fetch is in flight — call it from the
  fetching task.
- `ota_http_progress_get()` may be read from another task; the value may be
  stale on return, which is what a progress bar wants.
- The body is read into a 1 KB stack buffer inside the fetching task, so that
  task needs the headroom on top of the TLS stack.

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

- [../behavior/ota-download-flow.md](../behavior/ota-download-flow.md) — the fetch algorithm
