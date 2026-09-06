/**
 * @file    bsp.h
 * @author  dtbao
 * @date    2026-09-06
 * @brief   Board pin map, clock and flash geometry for product 0xF001.
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

#ifndef BSP_H
#define BSP_H

/* ------------------------------ Includes ------------------------------- */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------- Constants & macros ------------------------- */

#define BSP_VERSION_MAJOR 0
#define BSP_VERSION_MINOR 1
#define BSP_VERSION_PATCH 0

/** A pin field set to this means the board does not wire that function. */
#define BSP_GPIO_NONE (-1)

/* -------------------------------- Types -------------------------------- */

/**
 * @brief   Status of a BSP call.
 *
 * The BSP sits under the middleware layer and must not include `fw.h`
 * (R-LAY-01), so it carries its own code space. The generic values
 * `-1 .. -19` mean exactly what they mean everywhere else (R-ERR-03), which
 * is what lets a caller map them onto `fw_err_t` one for one.
 */
typedef enum {
    BSP_OK         = 0,   /**< The call succeeded.                          */
    BSP_ERR_PARAM  = -1,  /**< Caller passed something impossible.          */
    BSP_ERR_STATE  = -2,  /**< Legal call, wrong lifecycle state.           */
    BSP_ERR_IO     = -7,  /**< The vendor SDK refused the operation.        */
    BSP_ERR_NO_PIN = -20, /**< The board does not wire that function.       */
} bsp_err_t;

/**
 * @brief   Everything about this board that is not a register.
 *
 * Filled in by `bsp_init()` from the compiled-in board table. Nothing above
 * the driver layer may hold a pin number of its own (R-RPO-04): the day the
 * pinout changes, only this module changes.
 */
typedef struct {
    int32_t led_status_gpio;   /**< Update-in-progress LED, BSP_GPIO_NONE if absent. */
    bool led_active_high;      /**< true when driving the pin high lights the LED.   */
    uint32_t flash_size_bytes; /**< Total SPI flash on the board, from the part.     */
} bsp_board_t;

/** @brief Configuration for one BSP instance. */
typedef struct {
    uint8_t board_rev; /**< Board revision, 0 selects the default table entry. */
} bsp_cfg_t;

/** @brief BSP instance. Caller allocates; the module owns the contents. */
typedef struct {
    bool is_init;      /**< Lifecycle state, checked by every operation. */
    bsp_board_t board; /**< Resolved board description.                  */
} bsp_t;

/* ------------------------ Public function prototypes ------------------- */

/**
 * @brief   Returns a short, stable name for a BSP status code.
 * @param   err   any `bsp_err_t`; an unknown value yields "BSP_ERR_UNKNOWN"
 * @return  Pointer to a string literal with static lifetime; never NULL.
 * @note    Reentrant, callable from any task.
 */
const char *bsp_err_str(bsp_err_t err);

/**
 * @brief   Resolves the board description and claims the pins it names.
 * @param   dev   caller-allocated instance, zeroed by this call
 * @param   cfg   board selection; copied, not retained
 * @return  BSP_OK, BSP_ERR_PARAM on a NULL argument or an unknown revision,
 *          BSP_ERR_STATE when already initialized, BSP_ERR_IO if the SDK
 *          refused a pin.
 * @note    Call once, from one task, before any other BSP function. Leaves
 *          the hardware inactive (R-LFC-01, R-LFC-09).
 */
bsp_err_t bsp_init(bsp_t *dev, const bsp_cfg_t *cfg);

/**
 * @brief   Releases everything `bsp_init()` claimed.
 * @param   dev   instance, possibly only partly initialized
 * @return  BSP_OK, or BSP_ERR_PARAM when `dev` is NULL.
 * @note    Safe to call twice and on a partly built instance (R-LFC-04).
 */
bsp_err_t bsp_deinit(bsp_t *dev);

/**
 * @brief   Hands out the resolved board description.
 * @param   dev         initialized instance
 * @param[out] out_board  points into `dev` and stays valid until `bsp_deinit()`;
 *                        the caller must not free it or outlive `dev`
 * @return  BSP_OK, BSP_ERR_PARAM on a NULL argument, BSP_ERR_STATE before init.
 * @note    Reentrant; the description does not change after init.
 */
bsp_err_t bsp_board_get(const bsp_t *dev, const bsp_board_t **out_board);

/**
 * @brief   Drives the update-in-progress LED.
 * @param   dev     initialized instance
 * @param   is_on   true lights the LED, whatever polarity the board uses
 * @return  BSP_OK, BSP_ERR_PARAM on NULL, BSP_ERR_STATE before init,
 *          BSP_ERR_NO_PIN when this board has no status LED.
 * @note    Any task. Not callable from an ISR.
 */
bsp_err_t bsp_led_status_set(bsp_t *dev, bool is_on);

#ifdef __cplusplus
}
#endif

#endif /* BSP_H */

/*** end of file ***/
