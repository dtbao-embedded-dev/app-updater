/**
 * @file    bsp_priv.h
 * @date    2026-09-07
 * @brief   Internal contract between bsp.c and its per-chip port.
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

#ifndef BSP_PRIV_H
#define BSP_PRIV_H

/* ------------------------------ Includes ------------------------------- */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------- Constants & macros ------------------------- */

/* -------------------------------- Types -------------------------------- */

/**
 * @brief   The pins this chip fixes in silicon.
 *
 * Not board data and not a `bsp_board_t` field: no revision of any board can
 * move these, because they are not routed through the GPIO matrix. They vary
 * by **chip**, which is why they arrive from the port and not from the board
 * table (R-RPO-04 still holds - the numbers themselves come from the chip's
 * own SDK headers, not from a literal in this repo).
 *
 * A pin the chip does not have is `BSP_GPIO_NONE`.
 */
typedef struct {
    int32_t usb_dp_gpio;     /**< USB D+ pad of the internal USB PHY.        */
    int32_t usb_dm_gpio;     /**< USB D- pad of the internal USB PHY.        */
    int32_t console_tx_gpio; /**< UART0 TXD - where the boot log leaves.     */
    int32_t console_rx_gpio; /**< UART0 RXD.                                 */
} bsp_port_pins_t;

/* ------------------------ Public function prototypes ------------------- */

/**
 * @brief   Hands back the pins this chip fixes in silicon.
 * @return  The port's pin set, by value; every field is a compile-time
 *          constant, so the call cannot fail and has no status.
 * @note    Implemented once per chip in `src/port/bsp_<IDF_TARGET>.c`
 *          (R-LIB-02). Reentrant, callable before `bsp_init()`.
 */
bsp_port_pins_t bsp_port_pins_get(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_PRIV_H */

/*** end of file ***/
