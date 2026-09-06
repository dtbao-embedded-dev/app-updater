---
title: OTA Download Flow
category: behavior
order: 3
purpose: How one image fetch runs, from opening the connection to detecting a truncated body.
status: active
updated: 2026-09-06
source: middleware/ota_http/src/ota_http.c:99-187
confidence: confirmed
keywords: ota_http_fetch, drain_body, esp_http_client_open, fetch_headers, is_complete_data_received, received_bytes, OTA_HTTP_CHUNK_BYTES
---

# OTA Download Flow

> Open, check the status line, then drain the body into the caller's callback 1 KB at a time, treating a short body as a timeout rather than a success.

## What it does

1. Reject the call unless the instance is initialised and no fetch is already
   running.
2. Open the connection. On failure, convert the vendor status and return without
   touching the running flag.
3. Mark the instance running and reset the delivered-byte counter.
4. Fetch the response headers, which yields a content length, and read the HTTP
   status code.
5. Decide:
   - content length below zero → `FW_ERR_IO` (header fetch failed)
   - status is not 200 → `FW_ERR_IO`, logged with both the received and the
     expected status
   - otherwise → drain the body
6. Close the connection and clear the running flag **whatever the outcome**, then
   return the verdict.

## Draining the body

Loop, reading into a 1 KB stack buffer:

| Read result | Action |
|-------------|--------|
| below zero | Log the byte count reached, return `FW_ERR_IO` |
| zero | Ambiguous — see below |
| above zero | Call the consumer callback; abort on its failure, otherwise add to the counter and continue |

A zero-length read means either "the body is finished" or "the read timed out
with nothing to give". The two are told apart by asking the client whether the
complete body was received:

- complete → `FW_OK`
- not complete → log the byte count and return **`FW_ERR_TIMEOUT`**

That distinction is the point of the loop: a truncated image that reported
success would be written to flash and booted.

## Inputs → outputs

| Reads | Produces |
|-------|----------|
| The configured URL and cert | An HTTPS body |
| — | One callback invocation per chunk, in order |
| — | `received_bytes`, readable from another task while the fetch runs |

The module **never writes flash**. Where the bytes go is entirely the caller's
decision, which is what keeps this module in the middleware layer.

## Edge cases & error handling

- The consumer's failure status is returned **unchanged** by the fetch, so a
  caller can tell its own error apart from a transport error.
- The connection is closed on every path, including the non-200 path, so a
  failed fetch leaves the instance reusable.
- `timeout_ms` is a per-read budget, not a total budget for the download. A very
  slow but never-idle server can therefore exceed any wall-clock expectation.
  No total-duration cap exists; whether one is
  needed is an **open design decision**.
- The 1 KB buffer lives on the fetching task's stack.

## See also

- [../interface/ota-http-api.md](../interface/ota-http-api.md) — the contract, including the callback rules
- [update-cycle-fsm.md](update-cycle-fsm.md) — the state that will drive this
