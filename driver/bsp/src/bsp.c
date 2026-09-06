/**
 * @file    bsp.c
 * @date    2026-09-06
 * @brief   Board pin map, clock and flash geometry for product 0xF001.
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

/* ------------------------------ Includes ------------------------------- */

#include "bsp.h"

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_flash.h"
#include "esp_log.h"

#include <string.h>

/* --------------------------- Private macros ---------------------------- */

/* TODO(dtbao): fill these two in from the 0xF001 schematic before the first
 * board bring-up. They are the only pin literals in the repo (R-RPO-04) and
 * the values below are placeholders, not a measured pinout. */
#define BSP_REV_A_LED_STATUS_GPIO 2
#define BSP_REV_A_LED_ACTIVE_HIGH true

/* ---------------------------- Private types ---------------------------- */

/* ----------------------------- Static data ----------------------------- */

static const char *TAG = "bsp";

/** Board table, indexed by revision. Add a row rather than editing one.
 *  Only what the schematic decides lives here; `flash_size_bytes` is asked
 *  of the chip at init instead, so it cannot drift from the part fitted. */
static const bsp_board_t s_board_table[] = {
    [0] =
        {
            .led_status_gpio = BSP_REV_A_LED_STATUS_GPIO,
            .led_active_high = BSP_REV_A_LED_ACTIVE_HIGH,
        },
};

/* --------------------- Private function prototypes --------------------- */

static bsp_err_t from_esp_err(esp_err_t err);

/* -------------------------- Public functions --------------------------- */

const char *bsp_err_str(bsp_err_t err) {
    switch (err) {
        case BSP_OK:
            return "BSP_OK";
        case BSP_ERR_PARAM:
            return "BSP_ERR_PARAM";
        case BSP_ERR_STATE:
            return "BSP_ERR_STATE";
        case BSP_ERR_IO:
            return "BSP_ERR_IO";
        case BSP_ERR_NO_PIN:
            return "BSP_ERR_NO_PIN";
    }

    return "BSP_ERR_UNKNOWN";
}

bsp_err_t bsp_init(bsp_t *dev, const bsp_cfg_t *cfg) {
    if ((dev == NULL) || (cfg == NULL)) {
        return BSP_ERR_PARAM;
    }
    if (dev->is_init) {
        return BSP_ERR_STATE;
    }
    if (cfg->board_rev >= (sizeof(s_board_table) / sizeof(s_board_table[0]))) {
        return BSP_ERR_PARAM;
    }

    memset(dev, 0, sizeof(*dev));
    dev->board = s_board_table[cfg->board_rev];

    /* Read from the part rather than compiled in: a board built with the
     * wrong density, or a table nobody updated, is then a value that differs
     * from partitions.csv instead of one that silently agrees with it. */
    const esp_err_t flash_err = esp_flash_get_size(NULL, &dev->board.flash_size_bytes);
    if (flash_err != ESP_OK) {
        ESP_LOGE(TAG, "flash size query failed: esp_err=0x%x", (unsigned)flash_err);
        return from_esp_err(flash_err);
    }

    if (dev->board.led_status_gpio != BSP_GPIO_NONE) {
        const gpio_config_t io = {
            .pin_bit_mask = 1ULL << (uint32_t)dev->board.led_status_gpio,
            .mode         = GPIO_MODE_OUTPUT,
            .pull_up_en   = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE,
        };
        const esp_err_t err = gpio_config(&io);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "led gpio %ld config failed: esp_err=0x%x",
                     (long)dev->board.led_status_gpio, (unsigned)err);
            return from_esp_err(err);
        }
    }

    dev->is_init = true;
    ESP_LOGI(TAG, "board rev=%u flash=%luB led_gpio=%ld", (unsigned)cfg->board_rev,
             (unsigned long)dev->board.flash_size_bytes, (long)dev->board.led_status_gpio);
    return BSP_OK;
}

bsp_err_t bsp_deinit(bsp_t *dev) {
    if (dev == NULL) {
        return BSP_ERR_PARAM;
    }

    /* Repeatable and safe on a half-built instance (R-LFC-04): reset only the
     * pin init actually claimed, then forget the board. */
    if (dev->is_init && (dev->board.led_status_gpio != BSP_GPIO_NONE)) {
        (void)gpio_reset_pin((gpio_num_t)dev->board.led_status_gpio);
    }
    memset(dev, 0, sizeof(*dev));
    return BSP_OK;
}

bsp_err_t bsp_board_get(const bsp_t *dev, const bsp_board_t **out_board) {
    if ((dev == NULL) || (out_board == NULL)) {
        return BSP_ERR_PARAM;
    }
    if (!dev->is_init) {
        return BSP_ERR_STATE;
    }

    *out_board = &dev->board;
    return BSP_OK;
}

bsp_err_t bsp_led_status_set(bsp_t *dev, bool is_on) {
    if (dev == NULL) {
        return BSP_ERR_PARAM;
    }
    if (!dev->is_init) {
        return BSP_ERR_STATE;
    }
    if (dev->board.led_status_gpio == BSP_GPIO_NONE) {
        return BSP_ERR_NO_PIN;
    }

    const uint32_t level = (is_on == dev->board.led_active_high) ? 1U : 0U;
    const esp_err_t err  = gpio_set_level((gpio_num_t)dev->board.led_status_gpio, level);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "led set level=%lu failed: esp_err=0x%x", (unsigned long)level,
                 (unsigned)err);
        return from_esp_err(err);
    }
    return BSP_OK;
}

/* -------------------------- Private functions -------------------------- */

/* Arguments are checked at the public boundary (R-SRC-06); helpers assume it.
 * R-ERR-04: the vendor code stops here, logged at the call site above. */
static bsp_err_t from_esp_err(esp_err_t err) {
    switch (err) {
        case ESP_OK:
            return BSP_OK;
        case ESP_ERR_INVALID_ARG:
            return BSP_ERR_PARAM;
        case ESP_ERR_INVALID_STATE:
            return BSP_ERR_STATE;
        default:
            return BSP_ERR_IO;
    }
}

/*** end of file ***/
