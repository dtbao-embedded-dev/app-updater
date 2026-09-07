---
title: BSP API
category: interface
order: 2
purpose: The board contract - resolve the board description, claim its pins, drive the status LED.
status: active
updated: 2026-09-07
source: driver/bsp/include/bsp.h, driver/bsp/src/bsp_priv.h, driver/bsp/src/bsp.c, driver/bsp/src/port/bsp_esp32s3.c, driver/bsp/CMakeLists.txt
confidence: confirmed
keywords: bsp.h, bsp_init, bsp_deinit, bsp_board_get, bsp_led_status_set, bsp_err_str, bsp_board_t, bsp_cfg_t, BSP_GPIO_NONE, bsp_priv.h, bsp_port_pins_t, bsp_port_pins_get, bsp_esp32s3.c, USBPHY_DP_NUM, U0TXD_GPIO_NUM, soc/usb_pins.h, soc/uart_pins.h, IDF_TARGET, port layer
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
| `flash_size_bytes` | uint32 | Total SPI flash, filled in by `bsp_init()` from `esp_flash_get_size()` — not from the board table |

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

The board table holds only what the schematic decides — the LED pin and its
polarity. Flash size is asked of the part at init, so it cannot drift from the
density actually fitted, and a query failure fails `bsp_init()` rather than
yielding a plausible wrong number.

## The per-chip port

The module splits **board** facts from **chip** facts and keeps them in
different files. Nothing in the repo re-states a number the chip's own SDK
already publishes:

| Fact | Varies by | Where it comes from |
|------|-----------|---------------------|
| LED pin, LED polarity | board revision | `s_board_table[]` in `bsp.c`, indexed by `board_rev` |
| Flash density | the part fitted | `esp_flash_get_size()` at init |
| USB D+/D− pads, UART0 TX/RX | **chip** | `bsp_port_pins_get()` in `src/port/bsp_<IDF_TARGET>.c`, which reads `soc/usb_pins.h` and `soc/uart_pins.h` |
| GPIO and flash register access | chip | `esp_driver_gpio` / `spi_flash`, already per-target |

### The port contract

`src/bsp_priv.h` (R-MOD-06) is the whole interface between the two halves — one
type and one function:

| Name | Signature (text) | Does |
|------|------------------|------|
| `bsp_port_pins_t` | struct of four `int32_t`: `usb_dp_gpio`, `usb_dm_gpio`, `console_tx_gpio`, `console_rx_gpio` | The pins the chip fixes in silicon; `BSP_GPIO_NONE` for one the chip lacks |
| `bsp_port_pins_get` | `bsp_port_pins_t bsp_port_pins_get(void)` | Hands them back **by value** — every field is a compile-time constant, so there is no failure and no status to return |

`src/bsp.c` names no chip: it calls the port for the four numbers and logs them.
`src/port/bsp_esp32s3.c` holds the two `soc/` includes and the S3's
single-shared-USB-PHY story, because that story is different on a part with two
PHYs or none.

### Adding a chip

Write `src/port/bsp_<target>.c` implementing `bsp_priv.h`. Nothing else changes
— `CMakeLists.txt` resolves `src/port/bsp_${IDF_TARGET}.c` itself, and
`bsp.c` stays untouched (R-LIB-02: a port file, never an `#ifdef` in the logic).

A target with no port file **stops the build at configure time** with the file
to create — verified by moving `bsp_esp32s3.c` aside and reconfiguring:

```
driver/bsp has no port for IDF_TARGET 'esp32s3'.
Add src/port/bsp_esp32s3.c implementing src/bsp_priv.h.
```

That refusal is the point. Without it a retarget compiles `bsp.c` against
whatever `soc/` offers and logs pins the part may not have.

🔴 **Unverified against hardware:** the two board table values (`GPIO2`,
active-high) are placeholders carrying a TODO, never checked against a 0xF001
schematic. They are the single point to fix before any bring-up.

## See also

- [../data/error-code-model.md](../data/error-code-model.md) — `bsp_err_t` and its map to `fw_err_t`
