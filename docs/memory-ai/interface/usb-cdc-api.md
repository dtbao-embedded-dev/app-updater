---
title: USB CDC Transport API
category: interface
order: 9
purpose: The driver contract for the CDC-ACM byte pipe, its VID/PID, and the PHY it takes from USB-Serial-JTAG.
status: active
updated: 2026-09-07
source: driver/usb_cdc/include/usb_cdc.h, driver/usb_cdc/src/usb_cdc.c, driver/usb_cdc/idf_component.yml
confidence: confirmed
keywords: usb_cdc.h, usb_cdc_init, usb_cdc_deinit, usb_cdc_write, usb_cdc_is_mounted, usb_cdc_err_str, usb_cdc_err_t, USB_CDC_VID, USB_CDC_PID, esp_tinyusb, TinyUSB, internal PHY
---

# USB CDC Transport API

> A byte pipe to a PC on the USB-OTG peripheral, enumerating as
> `A331:F001`. Bringing it up **turns USB-Serial-JTAG off**.

## Responsibility

`driver/usb_cdc` owns the vendor USB stack and nothing above it: bytes in, bytes
out. It never sees a frame. Sitting in the driver layer it must not include
`fw.h` (R-LAY-01), so it carries `usb_cdc_err_t` — the same arrangement
`driver/bsp` has.

## Identity

| Constant | Value | Why it is a constant |
|----------|-------|----------------------|
| `USB_CDC_VID` | `0xA331` | Fixed for this product. A knob for a value that never changes is a knob that can be set wrong |
| `USB_CDC_PID` | `0xF001` | Same, and it matches the product id |
| `USB_CDC_WRITE_TIMEOUT_MS` | 1000 | per flush attempt |

They are set at **runtime**, through the descriptor the module supplies to
`esp_tinyusb`, not through `CONFIG_TINYUSB_DESC_CUSTOM_*` — so there is no
menuconfig value that can drift from the header. Only the device descriptor and
the five string descriptors are ours; the configuration descriptor is left NULL
so the component builds its own CDC one and owns the endpoint numbering.

## Status codes

| Code | Value | Meaning |
|------|-------|---------|
| `USB_CDC_OK` | 0 | succeeded |
| `USB_CDC_ERR_PARAM` | -1 | impossible argument |
| `USB_CDC_ERR_STATE` | -2 | wrong lifecycle state |
| `USB_CDC_ERR_TIMEOUT` | -3 | the host stopped draining the TX FIFO |
| `USB_CDC_ERR_IO` | -7 | the vendor SDK refused |
| `USB_CDC_ERR_NO_MEM` | -9 | the stack could not be built |
| `USB_CDC_ERR_NOT_MOUNTED` | -20 | no host has configured the device |

`-1 .. -19` keep their project-wide meanings (R-ERR-03), so a caller maps them
onto `fw_err_t` one for one; `-20` upward is this module's own space.

## Signatures

```c
typedef void (*usb_cdc_rx_cb_t)(void *ctx, const uint8_t *data, size_t len);

const char *usb_cdc_err_str(usb_cdc_err_t err);
usb_cdc_err_t usb_cdc_init(usb_cdc_t *dev, const usb_cdc_cfg_t *cfg);
usb_cdc_err_t usb_cdc_deinit(usb_cdc_t *dev);
usb_cdc_err_t usb_cdc_write(usb_cdc_t *dev, const uint8_t *data, size_t len);
bool usb_cdc_is_mounted(const usb_cdc_t *dev);
```

`usb_cdc_cfg_t` carries only `on_rx` (required) and `rx_ctx`.

| Function | Contract |
|----------|----------|
| `usb_cdc_init` | Installs the stack and enumerates. **One instance per image, enforced**: the TinyUSB CDC callback signature carries no context pointer, so the module has to keep a static handle to the instance those callbacks belong to. OK means the stack runs, not that a host is attached |
| `usb_cdc_deinit` | Tears the stack down and releases the PHY. Repeatable, safe on a half-built instance. Does **not** restore USB-Serial-JTAG |
| `usb_cdc_write` | Loops queue-then-flush, because the TX FIFO is 512 bytes and a maximum reply is 32788. A host that opened the port and stopped reading is `USB_CDC_ERR_TIMEOUT` after three stalled attempts, not a hang |
| `usb_cdc_is_mounted` | May be stale on return, which is all a "should I bother replying" check needs. Tracked from the driver's ATTACHED/DETACHED events into a `volatile bool` |

`on_rx` runs on the **TinyUSB service task**, which is also the only task
draining the CDC RX FIFO. Blocking there stalls the whole channel, so the
reference wiring does nothing in it but push bytes into a stream buffer — see
[../behavior/boot-and-bring-up.md](../behavior/boot-and-bring-up.md).

## The dependency, and the PHY

ESP-IDF v6.1 ships **no USB device stack**: its `components/` holds
`esp_driver_usb_serial_jtag`, `esp_hal_usb` and `esp_usb_cdc_rom_console` and no
TinyUSB. So `driver/usb_cdc/idf_component.yml` declares
`espressif/esp_tinyusb: "~2.0.0"` — the repo's first managed dependency, which
also pulls `espressif/tinyusb`. `managed_components/` and `dependencies.lock`
are gitignored, so a fresh clone resolves them on its first build and needs
network for it.

**The ESP32-S3 has one internal USB PHY, time-division shared between USB-OTG
and USB-Serial-JTAG** (TRM 32.3.1 and 33.3.1) — only one works at a time, and
`tinyusb_driver_install()` switches it to USB-OTG with no way to ask otherwise.
Consequences, all of them deliberate:

- the console and boot log are on **UART0** (GPIO43/44), not on USB;
- `tool-esp.py flash` loses its USB auto-download reset — flash over UART0, or
  hold BOOT;
- `EFUSE_USB_PHY_SEL` must **never** be burned: it is one-way and would take
  USB-Serial-JTAG download away in the bootloader too.

The repo does not state those pin numbers anywhere. They arrive from the BSP's
per-chip port: `driver/bsp/src/port/bsp_esp32s3.c` includes `soc/usb_pins.h` and
`soc/uart_pins.h` and returns `USBPHY_DP_NUM` / `USBPHY_DM_NUM` /
`U0TXD_GPIO_NUM` / `U0RXD_GPIO_NUM` straight from the SDK — 20/19 and 43/44 on
this target. They are not board-table rows because no board revision can move
them, and not constants of ours because ESP-IDF already publishes them per
`IDF_TARGET` — see [bsp-api.md](bsp-api.md).

## See also

- [bsp-api.md](bsp-api.md) — where the pins live
- [../architecture/layering-and-dependencies.md](../architecture/layering-and-dependencies.md) — the third error code space
- [../rule/known-deviations.md](../rule/known-deviations.md) — the console move, recorded as a deviation
