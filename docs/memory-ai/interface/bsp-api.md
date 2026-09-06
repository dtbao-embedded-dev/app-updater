---
title: BSP API
category: interface
order: 2
purpose: The board contract - resolve the board description, claim its pins, drive the status LED.
status: active
updated: 2026-09-06
source: driver/bsp/include/bsp.h, driver/bsp/src/bsp.c:51-153
confidence: confirmed
keywords: bsp.h, bsp_init, bsp_deinit, bsp_board_get, bsp_led_status_set, bsp_err_str, bsp_board_t, bsp_cfg_t, BSP_GPIO_NONE
---

# BSP API

> The only module allowed to know a pin number; everything above it takes the board description by pointer.

## Contract

| Name | Signature (text) | Does | Returns / errors |
|------|------------------|------|------------------|
| `bsp_err_str` | `const char *bsp_err_str(bsp_err_t err)` | Names a BSP status | Static literal, never NULL |
| `bsp_init` | `bsp_err_t bsp_init(bsp_t *dev, const bsp_cfg_t *cfg)` | Resolves the board row, zeroes the instance, configures the LED pin as output | `BSP_OK`; `BSP_ERR_PARAM` on NULL or unknown revision; `BSP_ERR_STATE` if already initialised; `BSP_ERR_IO` if the SDK refused the pin |
| `bsp_deinit` | `bsp_err_t bsp_deinit(bsp_t *dev)` | Resets any pin `init` claimed and forgets the board | `BSP_OK`; `BSP_ERR_PARAM` on NULL |
| `bsp_board_get` | `bsp_err_t bsp_board_get(const bsp_t *dev, const bsp_board_t **out_board)` | Hands out the resolved description | `BSP_OK`; `BSP_ERR_PARAM` on NULL; `BSP_ERR_STATE` before init |
| `bsp_led_status_set` | `bsp_err_t bsp_led_status_set(bsp_t *dev, bool is_on)` | Drives the status LED at the board's polarity | `BSP_OK`; `BSP_ERR_PARAM`/`BSP_ERR_STATE`; `BSP_ERR_NO_PIN` when the board has no LED |

## Data it exposes

`bsp_board_t` — everything about the board that is not a register:

| Field | Type | Meaning |
|-------|------|---------|
| `led_status_gpio` | int32 | Update-in-progress LED, or `BSP_GPIO_NONE` (-1) when absent |
| `led_active_high` | bool | True when driving the pin high lights the LED |
| `flash_size_bytes` | uint32 | Total SPI flash on the board |

`bsp_cfg_t` carries one field, `board_rev` (uint8), which indexes a compiled-in
board table. `0` is the only row that exists today.

## Contract rules

- The pointer from `bsp_board_get()` points **into** the caller's `bsp_t`. It
  stays valid until `bsp_deinit()` and must not be freed or outlive the
  instance.
- `bsp_led_status_set()` may be called from any task but not from an ISR.
- `BSP_ERR_NO_PIN` is an expected outcome on a board without an LED, not a
  failure. Callers treat it as success — `on_updater_state()` in the app does.
- Everything else follows the shared lifecycle rules below.

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

🔴 **Unverified against hardware:** the board table values (`GPIO2`, active-high, 4 MB) are placeholders
carrying a TODO, never checked against a 0xF001 schematic. They are the single
point to fix before any bring-up.

## See also

- [../data/error-code-model.md](../data/error-code-model.md) — `bsp_err_t` and its map to `fw_err_t`
