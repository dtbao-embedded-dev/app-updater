/**
 * @file    usb_cdc.h
 * @date    2026-09-06
 * @brief   USB CDC-ACM command channel for product 0xF001: a byte pipe to a
 *          PC host on the USB-OTG peripheral.
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

#ifndef USB_CDC_H
#define USB_CDC_H

/* ------------------------------ Includes ------------------------------- */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------- Constants & macros ------------------------- */

/* The identity a PC binds its driver to. Fixed for this product, so they are
 * constants and not configuration: a knob for a value that never changes is a
 * knob that can be set wrong. The day a second product needs different ones is
 * the day to make them a field. */
#define USB_CDC_VID 0xA331U
#define USB_CDC_PID 0xF001U

/** How long a write waits for the host to drain the TX FIFO, per attempt. */
#define USB_CDC_WRITE_TIMEOUT_MS 1000U

/* -------------------------------- Types -------------------------------- */

/**
 * @brief   Status of a USB CDC call.
 *
 * This module sits in the driver layer and must not include `fw.h`
 * (R-LAY-01), so it carries its own code space. The generic values
 * `-1 .. -19` mean exactly what they mean everywhere else (R-ERR-03), which
 * is what lets a caller map them onto `fw_err_t` one for one.
 */
typedef enum {
    USB_CDC_OK              = 0,   /**< The call succeeded.                   */
    USB_CDC_ERR_PARAM       = -1,  /**< Caller passed something impossible.   */
    USB_CDC_ERR_STATE       = -2,  /**< Legal call, wrong lifecycle state.    */
    USB_CDC_ERR_TIMEOUT     = -3,  /**< The host did not drain the TX FIFO.   */
    USB_CDC_ERR_IO          = -7,  /**< The vendor SDK refused the operation. */
    USB_CDC_ERR_NO_MEM      = -9,  /**< The stack could not be built.         */
    USB_CDC_ERR_NOT_MOUNTED = -20, /**< No host has configured the device.    */
} usb_cdc_err_t;

/**
 * @brief   Receives bytes as they arrive from the host.
 * @param   ctx    the `rx_ctx` supplied at init
 * @param   data   received bytes; valid only for the duration of the call, so
 *                 the callee copies or consumes them before returning
 * @param   len    1 .. 64
 * @note    Runs in the TinyUSB service task, never in an ISR. **Blocking here
 *          stalls the whole command channel**, because that task is also the
 *          only one draining the CDC RX FIFO: the reference wiring does
 *          nothing here but hand the bytes to a queue.
 */
typedef void (*usb_cdc_rx_cb_t)(void *ctx, const uint8_t *data, size_t len);

/** @brief Configuration for the one CDC instance. */
typedef struct {
    usb_cdc_rx_cb_t on_rx; /**< Required; see the typedef for context.      */
    void *rx_ctx;          /**< Passed back to `on_rx` unchanged.           */
} usb_cdc_cfg_t;

/** @brief CDC instance. Caller allocates; the module owns the contents. */
typedef struct {
    bool is_init;             /**< Lifecycle state, checked by operations.  */
    volatile bool is_mounted; /**< A host has configured the device. Written
                               *   from the TinyUSB task and read from
                               *   another, so never cached in a register.  */
    usb_cdc_cfg_t cfg;        /**< Copy of the config.                      */
} usb_cdc_t;

/* ------------------------ Public function prototypes ------------------- */

/**
 * @brief   Returns a short, stable name for a USB CDC status code.
 * @param   err   any `usb_cdc_err_t`; an unknown value yields
 *                "USB_CDC_ERR_UNKNOWN"
 * @return  Pointer to a string literal with static lifetime; never NULL.
 * @note    Reentrant, callable from any task.
 */
const char *usb_cdc_err_str(usb_cdc_err_t err);

/**
 * @brief   Brings the device stack up and enumerates as a CDC-ACM serial port.
 *
 * Installing the stack claims the chip's single internal USB PHY for the
 * USB-OTG peripheral, which **disables USB-Serial-JTAG** - so the console has
 * to be somewhere else by the time this runs. See the file comment in
 * `workspace/0xF001/sdkconfig.defaults`.
 *
 * @param   dev   caller-allocated instance, zeroed by this call
 * @param   cfg   copied into the instance; `on_rx` is required
 * @return  USB_CDC_OK, USB_CDC_ERR_PARAM on a NULL argument or a NULL `on_rx`,
 *          USB_CDC_ERR_STATE when already initialized or when a second
 *          instance is attempted, USB_CDC_ERR_NO_MEM when the stack could not
 *          be built, USB_CDC_ERR_IO when the SDK refused.
 * @note    **One instance per image**, enforced here: the TinyUSB callbacks
 *          carry no context pointer, so the module has to keep a static handle
 *          to the instance they belong to. Call once, from one task, before
 *          any other function here. Returning OK means the stack is running,
 *          not that a host is attached - poll `usb_cdc_is_mounted()` for that.
 */
usb_cdc_err_t usb_cdc_init(usb_cdc_t *dev, const usb_cdc_cfg_t *cfg);

/**
 * @brief   Tears the device stack down and releases the USB PHY.
 * @param   dev   instance, possibly only partly initialized
 * @return  USB_CDC_OK, or USB_CDC_ERR_PARAM when `dev` is NULL.
 * @note    Safe to call twice and on a partly built instance (R-LFC-04). Does
 *          not restore USB-Serial-JTAG: nothing here reconnects the PHY to it.
 */
usb_cdc_err_t usb_cdc_deinit(usb_cdc_t *dev);

/**
 * @brief   Sends bytes to the host, blocking until they are all queued.
 *
 * The TX FIFO is far smaller than a maximum-size response, so this loops:
 * queue what fits, flush, repeat. A host that opened the port and stopped
 * reading is a timeout, not a hang.
 *
 * @param   dev    initialized instance; read only, hence the const - this
 *                 sends bytes to the host, it does not change the instance
 * @param   data   bytes to send; copied into the FIFO, not retained
 * @param   len    number of bytes, 1 or more
 * @return  USB_CDC_OK when everything was queued and flushed,
 *          USB_CDC_ERR_PARAM on a NULL argument or a zero length,
 *          USB_CDC_ERR_STATE before init, USB_CDC_ERR_NOT_MOUNTED when no host
 *          is attached, USB_CDC_ERR_TIMEOUT when the host stopped draining,
 *          USB_CDC_ERR_IO on an SDK failure.
 * @note    Blocks the calling task. One caller only; not callable from an ISR.
 */
usb_cdc_err_t usb_cdc_write(const usb_cdc_t *dev, const uint8_t *data, size_t len);

/**
 * @brief   Reports whether a host has configured the device.
 * @param   dev   instance, possibly NULL
 * @return  true when a host is attached and the interface is up; false when
 *          `dev` is NULL, before init, or with no cable.
 * @note    Readable from any task; the value may be stale on return, which is
 *          all a "should I bother replying" check needs.
 */
bool usb_cdc_is_mounted(const usb_cdc_t *dev);

#ifdef __cplusplus
}
#endif

#endif /* USB_CDC_H */

/*** end of file ***/
