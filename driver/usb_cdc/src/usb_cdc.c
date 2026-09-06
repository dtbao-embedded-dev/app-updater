/**
 * @file    usb_cdc.c
 * @date    2026-09-06
 * @brief   USB CDC-ACM command channel for product 0xF001: a byte pipe to a
 *          PC host on the USB-OTG peripheral.
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

/* ------------------------------ Includes ------------------------------- */

#include "usb_cdc.h"

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "tinyusb.h"
#include "tinyusb_cdc_acm.h"
#include "tinyusb_default_config.h"

#include <string.h>

/* --------------------------- Private macros ---------------------------- */

/** One CDC endpoint's worth of bytes, drained on the TinyUSB task's stack. */
#define RX_CHUNK_BYTES 64U

/* A write that queues nothing this many times in a row gives up. The FIFO only
 * stays full while the host is not reading, and each attempt already waited
 * USB_CDC_WRITE_TIMEOUT_MS in the flush, so this bounds a stalled host at a
 * few seconds instead of forever. */
#define WRITE_STALL_LIMIT 3U

/* ---------------------------- Private types ---------------------------- */

/* ----------------------------- Static data ----------------------------- */

static const char *TAG = "usb_cdc";

/* The one instance, and the reason this module allows only one: the TinyUSB
 * CDC callback signature is `void (int itf, cdcacm_event_t *)` - no context
 * pointer - so there is nowhere to put the instance except here (R-SRC-03).
 * The device event callback does get an argument and uses it. */
static usb_cdc_t *s_dev;

/* Only the device descriptor and the strings are ours. The configuration
 * descriptor is left NULL on purpose so esp_tinyusb supplies its own CDC one,
 * built from CONFIG_TINYUSB_CDC_*: hand-writing a config descriptor here would
 * duplicate endpoint numbering the component already gets right. */
static const tusb_desc_device_t s_desc_device = {
    .bLength         = (uint8_t)sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB          = 0x0200,

    /* Interface Association Descriptor, which CDC needs: the USB spec requires
     * an IAD device to declare the common class and the IAD protocol. */
    .bDeviceClass    = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,

    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor  = (uint16_t)USB_CDC_VID,
    .idProduct = (uint16_t)USB_CDC_PID,
    .bcdDevice = 0x0100,

    .iManufacturer = 0x01,
    .iProduct      = 0x02,
    .iSerialNumber = 0x03,

    .bNumConfigurations = 0x01,
};

/* Index order is fixed by esp_tinyusb: 0 language, 1 manufacturer, 2 product,
 * 3 serial, 4 the CDC interface. The serial is a constant rather than the MAC,
 * so a bench keeps the same COM number when boards are swapped; a per-unit
 * identity on this channel is the GUID, which this product does not store. */
static const char *s_desc_str[] = {
    (const char[]){0x09, 0x04}, /* 0: English (0x0409), as two raw bytes */
    "Baotd",                    /* 1: iManufacturer                      */
    "App Updater 0xF001",       /* 2: iProduct                           */
    "0xF001",                   /* 3: iSerialNumber                      */
    "0xF001 command channel",   /* 4: CDC interface                      */
};

/* --------------------- Private function prototypes --------------------- */

static void on_usb_event(tinyusb_event_t *event, void *arg);
static void on_cdc_rx(int itf, cdcacm_event_t *event);
static usb_cdc_err_t from_esp_err(esp_err_t err);

/* -------------------------- Public functions --------------------------- */

const char *usb_cdc_err_str(usb_cdc_err_t err) {
    /* No `default` label: -Wswitch-enum (R-BLD-01) then fails the build when a
     * code is added here without a name. The fallthrough after the switch
     * covers a value cast in from outside. */
    switch (err) {
        case USB_CDC_OK:
            return "USB_CDC_OK";
        case USB_CDC_ERR_PARAM:
            return "USB_CDC_ERR_PARAM";
        case USB_CDC_ERR_STATE:
            return "USB_CDC_ERR_STATE";
        case USB_CDC_ERR_TIMEOUT:
            return "USB_CDC_ERR_TIMEOUT";
        case USB_CDC_ERR_IO:
            return "USB_CDC_ERR_IO";
        case USB_CDC_ERR_NO_MEM:
            return "USB_CDC_ERR_NO_MEM";
        case USB_CDC_ERR_NOT_MOUNTED:
            return "USB_CDC_ERR_NOT_MOUNTED";
    }

    return "USB_CDC_ERR_UNKNOWN";
}

usb_cdc_err_t usb_cdc_init(usb_cdc_t *dev, const usb_cdc_cfg_t *cfg) {
    if ((dev == NULL) || (cfg == NULL) || (cfg->on_rx == NULL)) {
        return USB_CDC_ERR_PARAM;
    }
    if (dev->is_init || (s_dev != NULL)) {
        return USB_CDC_ERR_STATE;
    }

    memset(dev, 0, sizeof(*dev));
    dev->cfg = *cfg;
    s_dev    = dev;

    /* Installing the stack switches the internal USB PHY from USB-Serial-JTAG
     * to USB-OTG. There is no way to keep both on this chip, and no flag to
     * ask for one: the switch happens inside the install. */
    tinyusb_config_t tusb_cfg        = TINYUSB_DEFAULT_CONFIG(on_usb_event, dev);
    tusb_cfg.descriptor.device       = &s_desc_device;
    tusb_cfg.descriptor.string       = s_desc_str;
    tusb_cfg.descriptor.string_count = (int)(sizeof(s_desc_str) / sizeof(s_desc_str[0]));

    esp_err_t err = tinyusb_driver_install(&tusb_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "tinyusb_driver_install failed: esp_err=0x%x", (unsigned)err);
        s_dev = NULL;
        return from_esp_err(err);
    }

    const tinyusb_config_cdcacm_t acm_cfg = {
        .cdc_port                     = TINYUSB_CDC_ACM_0,
        .callback_rx                  = on_cdc_rx,
        .callback_rx_wanted_char      = NULL,
        .callback_line_state_changed  = NULL,
        .callback_line_coding_changed = NULL,
    };
    err = tinyusb_cdcacm_init(&acm_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "tinyusb_cdcacm_init failed: esp_err=0x%x", (unsigned)err);
        (void)tinyusb_driver_uninstall();
        s_dev = NULL;
        return from_esp_err(err);
    }

    dev->is_init = true;
    ESP_LOGI(TAG, "cdc-acm up on usb-otg, vid=0x%04X pid=0x%04X (usb-serial-jtag is now off)",
             (unsigned)USB_CDC_VID, (unsigned)USB_CDC_PID);
    return USB_CDC_OK;
}

usb_cdc_err_t usb_cdc_deinit(usb_cdc_t *dev) {
    if (dev == NULL) {
        return USB_CDC_ERR_PARAM;
    }

    /* Repeatable and safe on a half-built instance (R-LFC-04). */
    if (dev->is_init) {
        (void)tinyusb_cdcacm_deinit((int)TINYUSB_CDC_ACM_0);
        (void)tinyusb_driver_uninstall();
    }
    if (s_dev == dev) {
        s_dev = NULL;
    }
    memset(dev, 0, sizeof(*dev));
    return USB_CDC_OK;
}

usb_cdc_err_t usb_cdc_write(usb_cdc_t *dev, const uint8_t *data, size_t len) {
    if ((dev == NULL) || (data == NULL) || (len == 0U)) {
        return USB_CDC_ERR_PARAM;
    }
    if (!dev->is_init) {
        return USB_CDC_ERR_STATE;
    }
    if (!dev->is_mounted) {
        return USB_CDC_ERR_NOT_MOUNTED;
    }

    size_t sent      = 0U;
    uint32_t stalled = 0U;

    while (sent < len) {
        const size_t queued =
            tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0, &data[sent], len - sent);
        sent += queued;

        /* Flush even on a zero-length queue: that is exactly the case where the
         * FIFO is full and the host has to be given a chance to drain it. */
        const esp_err_t err =
            tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, pdMS_TO_TICKS(USB_CDC_WRITE_TIMEOUT_MS));
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "write_flush after %u/%u bytes: esp_err=0x%x", (unsigned)sent,
                     (unsigned)len, (unsigned)err);
            return from_esp_err(err);
        }

        if (queued == 0U) {
            stalled += 1U;
            if (stalled >= WRITE_STALL_LIMIT) {
                ESP_LOGW(TAG, "host not draining, gave up after %u/%u bytes", (unsigned)sent,
                         (unsigned)len);
                return USB_CDC_ERR_TIMEOUT;
            }
        } else {
            stalled = 0U;
        }
    }

    return USB_CDC_OK;
}

bool usb_cdc_is_mounted(const usb_cdc_t *dev) {
    if (dev == NULL) {
        return false;
    }
    return dev->is_init && dev->is_mounted;
}

/* -------------------------- Private functions -------------------------- */

/* Arguments are checked at the public boundary (R-SRC-06); helpers assume it. */

/* Runs on the TinyUSB task. Only a flag is touched, so nothing here can stall
 * the channel. */
static void on_usb_event(tinyusb_event_t *event, void *arg) {
    usb_cdc_t *dev = (usb_cdc_t *)arg;

    if ((event == NULL) || (dev == NULL)) {
        return;
    }

    switch (event->id) {
        case TINYUSB_EVENT_ATTACHED:
            dev->is_mounted = true;
            break;
        case TINYUSB_EVENT_DETACHED:
            dev->is_mounted = false;
            break;
    }
}

/* Runs on the TinyUSB task, which is also the only task draining the RX FIFO -
 * so this drains it fully and hands the bytes straight on, and the sink is
 * documented as non-blocking. */
static void on_cdc_rx(int itf, cdcacm_event_t *event) {
    (void)event;

    usb_cdc_t *dev = s_dev;
    if ((dev == NULL) || (dev->cfg.on_rx == NULL)) {
        return;
    }

    for (;;) {
        uint8_t buf[RX_CHUNK_BYTES];
        size_t got = 0U;

        if (tinyusb_cdcacm_read((tinyusb_cdcacm_itf_t)itf, buf, sizeof(buf), &got) != ESP_OK) {
            return;
        }
        if (got == 0U) {
            return;
        }
        dev->cfg.on_rx(dev->cfg.rx_ctx, buf, got);
    }
}

/* R-ERR-04: the vendor code stops here, logged at the call site above. */
static usb_cdc_err_t from_esp_err(esp_err_t err) {
    switch (err) {
        case ESP_OK:
            return USB_CDC_OK;
        case ESP_ERR_INVALID_ARG:
            return USB_CDC_ERR_PARAM;
        case ESP_ERR_INVALID_STATE:
            return USB_CDC_ERR_STATE;
        case ESP_ERR_TIMEOUT:
            return USB_CDC_ERR_TIMEOUT;
        case ESP_ERR_NO_MEM:
            return USB_CDC_ERR_NO_MEM;
        default:
            return USB_CDC_ERR_IO;
    }
}

/*** end of file ***/
