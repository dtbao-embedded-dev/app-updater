/**
 * @file    bsp_esp32s3.c
 * @date    2026-09-07
 * @brief   ESP32-S3 port: the pins this chip fixes in silicon.
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

/* ------------------------------ Includes ------------------------------- */

#include "bsp_priv.h"

/* The two chip-specific includes of the whole repo, and the reason this file
 * exists (R-LIB-02): ESP-IDF resolves both to
 * `components/soc/<IDF_TARGET>/include/soc/`, so the numbers below are the
 * chip's own statement of its pinout, never a copy of the datasheet.
 *
 * `uart_pins.h` exists for every target; `usb_pins.h` exists for esp32s2 and
 * esp32s3 only - the two parts whose internal USB PHY pads are named in
 * `soc/`. Keeping both in the port rather than in `bsp.c` is what makes
 * `bsp.c` chip-agnostic: a part without an internal PHY gets a port that
 * reports BSP_GPIO_NONE, not a file that fails to compile. */
#include "soc/uart_pins.h"
#include "soc/usb_pins.h"

/* --------------------------- Private macros ---------------------------- */

/* ---------------------------- Private types ---------------------------- */

/* ----------------------------- Static data ----------------------------- */

/* --------------------- Private function prototypes --------------------- */

/* -------------------------- Public functions --------------------------- */

/* The one thing worth knowing before probing an S3 board: **this chip has a
 * single internal USB PHY, time-division shared between USB-OTG and
 * USB-Serial-JTAG** (TRM 32.3.1, 33.3.1). Both peripherals sit on the two pads
 * below and only one may drive them. This firmware gives the PHY to USB-OTG -
 * the only way to enumerate with our own VID and PID (`USB_CDC_VID` /
 * `USB_CDC_PID` in `driver/usb_cdc`), since USB-Serial-JTAG's `303A:1001` is
 * fixed in ROM. So USB-Serial-JTAG is off once the app runs and the console is
 * on UART0. `EFUSE_USB_PHY_SEL` must never be burned: it is one-way and would
 * take USB-Serial-JTAG download away in the bootloader too.
 *
 * A chip with two PHYs, or none, tells a different story in its own port. */
bsp_port_pins_t bsp_port_pins_get(void) {
    const bsp_port_pins_t pins = {
        .usb_dp_gpio     = USBPHY_DP_NUM,
        .usb_dm_gpio     = USBPHY_DM_NUM,
        .console_tx_gpio = U0TXD_GPIO_NUM,
        .console_rx_gpio = U0RXD_GPIO_NUM,
    };

    return pins;
}

/* -------------------------- Private functions -------------------------- */

/*** end of file ***/
