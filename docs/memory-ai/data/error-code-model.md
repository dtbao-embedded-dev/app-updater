---
title: Error Code Model
category: data
order: 1
purpose: The two status enums the firmware uses, their numeric ranges, and where a vendor code is converted.
status: active
updated: 2026-09-06
source: middleware/fw/include/fw.h, driver/bsp/include/bsp.h:33-49, middleware/storage/src/storage.c:209-227, middleware/ota_http/src/ota_http.c:189-205, driver/bsp/src/bsp.c:155-167
confidence: confirmed
keywords: fw_err_t, bsp_err_t, FW_OK, BSP_OK, FW_ERR_PARAM, FW_ERR_STATE, from_esp_err, esp_err_t
---

# Error Code Model

> Zero is success, every failure is negative, the range `-1 .. -19` means the same thing in every module, and a vendor status never leaves the module that produced it.

## Shape: `fw_err_t`

The project-wide status. Every fallible function in `application/` and
`middleware/` returns it; results leave through trailing out-parameters.

| Constant | Value | Meaning |
|----------|-------|---------|
| `FW_OK` | 0 | The call succeeded. |
| `FW_ERR_PARAM` | -1 | Caller passed something impossible. Caller has a bug. |
| `FW_ERR_STATE` | -2 | Legal call, wrong lifecycle moment. Caller called too early or twice. |
| `FW_ERR_TIMEOUT` | -3 | The peer or the bus did not answer in time. |
| `FW_ERR_NO_SPACE` | -4 | A buffer or an internal queue is full. |
| `FW_ERR_UNSUPPORTED` | -5 | The build or the hardware cannot do this. |
| `FW_ERR_NOT_FOUND` | -6 | The named record or partition is absent. |
| `FW_ERR_IO` | -7 | The transport or the flash itself failed. |
| `FW_ERR_CRC` | -8 | Stored or received data did not check out. |
| `FW_ERR_NO_MEM` | -9 | An allocation the call needed failed. |

## Shape: `bsp_err_t`

The driver layer cannot include a middleware header, so `driver/bsp/` carries
its own space. The generic values keep the same meanings, which is what makes
the map to `fw_err_t` one-for-one.

| Constant | Value | Meaning |
|----------|-------|---------|
| `BSP_OK` | 0 | The call succeeded. |
| `BSP_ERR_PARAM` | -1 | Same as `FW_ERR_PARAM`. |
| `BSP_ERR_STATE` | -2 | Same as `FW_ERR_STATE`. |
| `BSP_ERR_IO` | -7 | The vendor SDK refused the operation. |
| `BSP_ERR_NO_PIN` | -20 | Module-specific: this board does not wire that function. |

## Invariants

1. `0` is the only non-negative value. A truthiness test on a status is
   therefore obviously wrong rather than subtly wrong.
2. `-1 .. -19` is the shared generic range: identical meaning in every module.
   `-20 .. -99` is free for module-specific failures; `BSP_ERR_NO_PIN` is the
   only one so far.
3. **A numeric value is never reused for a second meaning.** Retire a code by
   leaving it defined and unused; codes reach field logs and support tickets.
4. Both enums must have a `..._err_str()` covering every value with no `default`
   label, so `-Wswitch-enum` fails the build when a code is added without a
   name. Each falls through to `"FW_ERR_UNKNOWN"` / `"BSP_ERR_UNKNOWN"` for a
   value cast in from outside.
5. `FW_ERR_PARAM` and `FW_ERR_STATE` must stay distinct: one says the caller has
   a bug, the other says the caller called at the wrong moment, and they lead to
   different fixes.

## Vendor conversion boundary

`esp_err_t` never escapes the module that called the SDK. Three modules each
carry a private `from_esp_err()` that logs the vendor value at the call site
then returns the module's own code:

| Module | Maps | Everything else |
|--------|------|-----------------|
| `bsp` | `ESP_ERR_INVALID_ARG`→`BSP_ERR_PARAM`, `ESP_ERR_INVALID_STATE`→`BSP_ERR_STATE` | `BSP_ERR_IO` |
| `storage` | plus `ESP_ERR_NVS_NOT_FOUND`→`FW_ERR_NOT_FOUND`, `ESP_ERR_NVS_NOT_ENOUGH_SPACE`→`FW_ERR_NO_SPACE`, `ESP_ERR_NO_MEM`→`FW_ERR_NO_MEM` | `FW_ERR_IO` |
| `ota_http` | plus `ESP_ERR_TIMEOUT`→`FW_ERR_TIMEOUT`, `ESP_ERR_NO_MEM`→`FW_ERR_NO_MEM` | `FW_ERR_IO` |

The `bsp_err_t` → `fw_err_t` map is applied in exactly one place, in
`bring_up_bsp()` in `application/app/src/app.c`.

## See also

- [../interface/fw-status-api.md](../interface/fw-status-api.md) — the `fw_err_str()` contract
- [../architecture/layering-and-dependencies.md](../architecture/layering-and-dependencies.md) — why there are two spaces
