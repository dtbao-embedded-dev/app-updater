/**
 * @file    app.c
 * @date    2026-09-06
 * @brief   Brings every module up in order and runs the update cycle.
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

/* ------------------------------ Includes ------------------------------- */

#include "app.h"

#include "app_priv.h"
#include "bsp.h"
#include "cfg.h"
#include "command.h"
#include "ota.h"
#include "protocol.h"
#include "storage.h"
#include "updater.h"
#include "usb_cdc.h"

#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --------------------------- Private macros ---------------------------- */

/* Set by this component's CMakeLists from `git rev-parse`. It is read at
 * CONFIGURE time, so it goes stale until CMake runs again - a banner may
 * name the commit the build tree was configured on, not the one checked out
 * now. Good enough to identify a field unit's image; not evidence in a
 * bisect. */
#ifndef APP_GIT_COMMIT
#define APP_GIT_COMMIT "unknown"
#endif

/* Bytes buffered between the USB service task and the dispatch task. The CDC
 * RX FIFO is 512 and the protocol is synchronous - a host sends one command and
 * waits - so this only has to absorb one frame arriving faster than the parser
 * walks it, which it does trivially. Sized generously anyway because 4 KB next
 * to the channel's 64 KB of frame buffers is not worth economising on. */
#define APP_USB_RX_BYTES 4096U

/* The dispatch task. It parses frames and runs handlers, the deepest of which
 * writes flash through esp_ota_write(); the frame buffers it works on live in
 * the context below rather than on this stack. Priority matches the TinyUSB
 * service task, so neither starves the other. */
#define APP_USB_TASK_STACK 4096U
#define APP_USB_TASK_PRIO  5U

/* ---------------------------- Private types ---------------------------- */

/** Everything the application owns, in one struct passed down at init
 *  (R-CFG-09). No module reaches for a global of its own. */
typedef struct {
    bsp_t bsp;
    storage_t storage;
    updater_t updater;
    cfg_t cfg;
    usb_cdc_t usb;
    command_t command;
    protocol_parser_t parser;
    StreamBufferHandle_t usb_rx;
} app_ctx_t;

/* ----------------------------- Static data ----------------------------- */

static const char *TAG = "app";

/* Genuinely per-image: there is one board and one update cycle (R-SRC-03).
 * Every module below still keeps its state in this struct rather than in file
 * statics of its own, so a second instance stays possible. */
static app_ctx_t s_ctx;

/* --------------------- Private function prototypes --------------------- */

static void print_banner(void);
static fw_err_t bring_up_storage(app_ctx_t *ctx);
static fw_err_t bring_up_bsp(app_ctx_t *ctx);
static fw_err_t bring_up_updater(app_ctx_t *ctx);
static fw_err_t bring_up_usb(app_ctx_t *ctx);
static fw_err_t cfg_store_load(void *ctx, void *out, size_t cap, size_t *out_len);
static fw_err_t cfg_store_save(void *ctx, const void *data, size_t len);
static fw_err_t from_storage_err(storage_err_t err);
static void on_usb_rx(void *ctx, const uint8_t *data, size_t len);
static fw_err_t on_usb_reply(void *ctx, const uint8_t *data, size_t len);
static bool is_slot_write_busy(void *ctx);
static void usb_dispatch_task(void *arg);
static void confirm_or_roll_back(void);
static void on_updater_state(void *ctx, updater_state_t state);
static uint32_t now_ms(void);

/* -------------------------- Public functions --------------------------- */

fw_err_t app_run(void) {
    print_banner();

    /* R-VER-08: decide the fate of a freshly flashed image before doing
     * anything that could make the decision impossible. */
    confirm_or_roll_back();

    memset(&s_ctx, 0, sizeof(s_ctx));

    /* Every module is brought up and its status checked before any traffic
     * starts (R-LFC-09). */
    fw_err_t err = bring_up_storage(&s_ctx);
    if (err != FW_OK) {
        ESP_LOGE(TAG, "storage bring-up failed: %s", fw_err_str(err));
        return err;
    }

    err = bring_up_bsp(&s_ctx);
    if (err != FW_OK) {
        ESP_LOGE(TAG, "bsp bring-up failed: %s", fw_err_str(err));
        return err;
    }

    err = bring_up_updater(&s_ctx);
    if (err != FW_OK) {
        ESP_LOGE(TAG, "updater bring-up failed: %s", fw_err_str(err));
        return err;
    }

    /* USB last, and deliberately after the updater: the very first frame may
     * be an UPG_BEGIN, whose refusal depends on asking the update cycle
     * whether it is already writing the slot. A channel that answered before
     * there was anything to ask would race on the first command it served. */
    err = bring_up_usb(&s_ctx);
    if (err != FW_OK) {
        ESP_LOGE(TAG, "usb bring-up failed: %s", fw_err_str(err));
        return err;
    }

    for (;;) {
        const fw_err_t step_err = updater_step(&s_ctx.updater, now_ms());
        if (step_err != FW_OK) {
            /* Logged once, here, by the layer that decides what to do about it
             * (R-LOG-04). The cycle carries its own retry budget. */
            ESP_LOGW(TAG, "update step: %s", fw_err_str(step_err));
        }
        vTaskDelay(pdMS_TO_TICKS(APP_TICK_MS));
    }
}

void app_main(void) {
    const fw_err_t err = app_run();

    /* app_run() returns only on a bring-up failure. Stopping loudly beats
     * running on with half a stack up (R-BLD-03). */
    ESP_LOGE(TAG, "app_run returned: %s", fw_err_str(err));
    abort();
}

/* -------------------------- Private functions -------------------------- */

/* Arguments are checked at the public boundary (R-SRC-06); helpers assume it. */

/* printf, not ESP_LOGI: this is the one block meant to be read by a person
 * looking at a terminal, so it carries no level, tag or timestamp and is not
 * filtered out by CONFIG_LOG_DEFAULT_LEVEL. */
static void print_banner(void) {
    const esp_app_desc_t *desc = esp_app_get_description();

    esp_chip_info_t chip;
    esp_chip_info(&chip);
    const unsigned rev = chip.revision;

    uint8_t mac[6] = {0};
    (void)bsp_mac_get(BSP_MAC_WIFI, mac);

    /* One product, one chip. A switch over esp_chip_model_t would trip
     * -Wswitch-enum on every model this repo will never be built for. */
    const char *model = (chip.model == CHIP_ESP32S3) ? "ESP32-S3" : "unknown";

    printf("\n");
    printf("Project name: %s\n", desc->project_name);
    printf("Version:      v%s\n", desc->version);
    printf("Commit hash:  %s\n", APP_GIT_COMMIT);
    printf("Time build:   %s - %s\n", desc->date, desc->time);
    printf("ESP-IDF:      %s\n", desc->idf_ver);
    printf("Chip type:    %s (revision v%u.%u)\n", model, rev / 100U, rev % 100U);
    printf("Features:     %s%s%s%s%u core%s, %u MHz\n",
           (chip.features & CHIP_FEATURE_WIFI_BGN) ? "Wi-Fi, " : "",
           (chip.features & CHIP_FEATURE_BLE) ? "BLE, " : "",
           (chip.features & CHIP_FEATURE_BT) ? "BT, " : "",
           (chip.features & CHIP_FEATURE_EMB_PSRAM) ? "embedded PSRAM, " : "", (unsigned)chip.cores,
           (chip.cores > 1) ? "s" : "", (unsigned)CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
    printf("MAC:          %02x:%02x:%02x:%02x:%02x:%02x\n", (unsigned)mac[0], (unsigned)mac[1],
           (unsigned)mac[2], (unsigned)mac[3], (unsigned)mac[4], (unsigned)mac[5]);
    printf("\n");
    fflush(stdout);
}

static fw_err_t bring_up_storage(app_ctx_t *ctx) {
    const storage_cfg_t st_cfg = storage_cfg_default();
    const storage_err_t err    = storage_init(&ctx->storage, &st_cfg);
    if (err != STORAGE_OK) {
        ESP_LOGE(TAG, "storage_init: %s", storage_err_str(err));
        return from_storage_err(err);
    }

    /* The settings themselves live in middleware/cfg; storage is only where
     * their bytes go, and these two wrappers are the only place in the image
     * that knows which of the two it is. A missing or damaged record is not a
     * failure - cfg_init() logs it, takes the compiled-in defaults and still
     * reports FW_OK (R-CFG-02, R-CFG-03) - so anything non-OK from here is a
     * real bring-up failure. */
    const cfg_store_t store = {
        .load = cfg_store_load,
        .save = cfg_store_save,
        .ctx  = ctx,
    };
    return cfg_init(&ctx->cfg, &store);
}

/* What makes middleware/cfg's persistence concrete. Kept as two one-line
 * wrappers rather than pointing cfg straight at storage_blob_*: their first
 * parameter is the whole application context, which is what lets the settings
 * be re-pointed at the reserved cfg_setting partition later without cfg or
 * storage changing at all. */
static fw_err_t cfg_store_load(void *ctx, void *out, size_t cap, size_t *out_len) {
    app_ctx_t *app = (app_ctx_t *)ctx;
    return from_storage_err(storage_blob_load(&app->storage, out, cap, out_len));
}

static fw_err_t cfg_store_save(void *ctx, const void *data, size_t len) {
    app_ctx_t *app = (app_ctx_t *)ctx;
    return from_storage_err(storage_blob_save(&app->storage, data, len));
}

/* The driver keeps its own code space (R-LAY-01) but shares the generic
 * -1..-19 meanings, so the map is one for one (R-ERR-03) and this is the only
 * place in the image that has to know both. STORAGE_ERR_CRC reaching cfg as
 * FW_ERR_CRC is what makes it fall back to the defaults rather than fail
 * bring-up on a record it could not have parsed. */
static fw_err_t from_storage_err(storage_err_t err) {
    /* Only the shared generic range -1..-19 maps one for one. A code from the
     * driver's own space (-20 and below) has no fw_err_t twin, so it arrives
     * as a plain I/O failure rather than as a number fw_err_str() cannot
     * name. */
    return ((int)err >= -19) ? (fw_err_t)err : FW_ERR_IO;
}

static fw_err_t bring_up_bsp(app_ctx_t *ctx) {
    const bsp_cfg_t cfg = {.board_rev = 0U};
    const bsp_err_t err = bsp_init(&ctx->bsp, &cfg);
    if (err != BSP_OK) {
        ESP_LOGE(TAG, "bsp_init: %s", bsp_err_str(err));
        /* The driver keeps its own code space (R-LAY-01) but shares the
         * generic -1..-19 meanings, so the map is one for one (R-ERR-03). */
        return (err == BSP_ERR_PARAM) ? FW_ERR_PARAM : FW_ERR_IO;
    }
    return FW_OK;
}

static fw_err_t bring_up_updater(app_ctx_t *ctx) {
    uint32_t interval_ms = 0U;
    fw_err_t err         = cfg_check_interval_ms_get(&ctx->cfg, &interval_ms);
    if (err != FW_OK) {
        return err;
    }

    /* updater_init() rejects an interval at or past its scheduling horizon,
     * and cfg refuses to hold or load one - both the setter and the record
     * validator check it. So this call can no longer fail on a stored value:
     * before cfg existed, a CRC-valid record with a large interval failed
     * bring-up here instead of falling back to the defaults. */
    updater_cfg_t up_cfg     = updater_cfg_default();
    up_cfg.check_interval_ms = interval_ms;
    up_cfg.on_state          = on_updater_state;
    up_cfg.on_state_ctx      = ctx;

    err = updater_init(&ctx->updater, &up_cfg);
    if (err != FW_OK) {
        return err;
    }
    return updater_start(&ctx->updater, now_ms());
}

/* R-VER-08: a new image gets exactly one boot to prove itself. Without this
 * call the bootloader reverts on the next reset, which is the safe default but
 * makes every good update look like a failed one. */
static void confirm_or_roll_back(void) {
    bool is_pending      = false;
    const ota_err_t read = ota_pending_verify_is(&is_pending);
    if (read != OTA_OK) {
        ESP_LOGW(TAG, "boot state: %s", ota_err_str(read));
        return;
    }
    if (!is_pending) {
        return;
    }

    /* SPEC-DEVIATION(R-VER-08): this confirms without running a self-test.
     * TODO(dtbao): run the real diagnostic here before the first field build.
     * An image that can confirm itself while broken defeats the rollback this
     * firmware exists to provide. */
    ESP_LOGW(TAG, "pending verify: confirming without a self-test (TODO)");
    const ota_err_t err = ota_mark_valid();
    if (err != OTA_OK) {
        ESP_LOGE(TAG, "confirm image: %s", ota_err_str(err));
    }
}

static void on_updater_state(void *ctx, updater_state_t state) {
    app_ctx_t *app = (app_ctx_t *)ctx;

    const bool is_busy  = (state == UPDATER_STATE_CHECKING) || (state == UPDATER_STATE_DOWNLOADING);
    const bsp_err_t err = bsp_led_status_set(&app->bsp, is_busy);
    if ((err != BSP_OK) && (err != BSP_ERR_NO_PIN)) {
        ESP_LOGW(TAG, "status led: %s", bsp_err_str(err));
    }
}

/* The command channel, in three parts: a byte pipe the USB task fills, a task
 * that drains it into the parser, and the dispatcher the parser feeds.
 *
 * Bringing the stack up claims the chip's single internal USB PHY for USB-OTG,
 * which turns USB-Serial-JTAG off - so the console is on UART0 by the time this
 * runs (see workspace/0xF001/sdkconfig.defaults). */
static fw_err_t bring_up_usb(app_ctx_t *ctx) {
    protocol_parser_reset(&ctx->parser);

    ctx->usb_rx = xStreamBufferCreate((size_t)APP_USB_RX_BYTES, 1U);
    if (ctx->usb_rx == NULL) {
        return FW_ERR_NO_MEM;
    }

    const command_cfg_t cmd_cfg = {
        .on_reply  = on_usb_reply,
        .reply_ctx = ctx,
        .is_busy   = is_slot_write_busy,
        .busy_ctx  = ctx,
    };
    fw_err_t err = command_init(&ctx->command, &cmd_cfg);
    if (err != FW_OK) {
        return err;
    }

    /* The task exists before the transport does, so no byte can arrive with
     * nothing to drain it (R-LFC-09). */
    if (xTaskCreate(usb_dispatch_task, "usb_cmd", APP_USB_TASK_STACK, ctx, APP_USB_TASK_PRIO,
                    NULL) != pdPASS) {
        return FW_ERR_NO_MEM;
    }

    const usb_cdc_cfg_t usb_cfg = {.on_rx = on_usb_rx, .rx_ctx = ctx};
    const usb_cdc_err_t usb_err = usb_cdc_init(&ctx->usb, &usb_cfg);
    if (usb_err != USB_CDC_OK) {
        ESP_LOGE(TAG, "usb_cdc_init: %s", usb_cdc_err_str(usb_err));
        /* The driver keeps its own code space (R-LAY-01) but shares the
         * generic -1..-19 meanings, so the map is one for one (R-ERR-03). */
        return (usb_err == USB_CDC_ERR_PARAM) ? FW_ERR_PARAM : FW_ERR_IO;
    }
    return FW_OK;
}

/* Runs on the TinyUSB service task, which is also the only task draining the
 * CDC RX FIFO - so it does exactly one thing and never blocks. A full buffer
 * drops bytes on purpose: the frame then fails its CRC and the parser resyncs,
 * which is a retry the host already knows how to do. Blocking here instead
 * would stall the whole channel. */
static void on_usb_rx(void *ctx, const uint8_t *data, size_t len) {
    app_ctx_t *app = (app_ctx_t *)ctx;

    const size_t sent = xStreamBufferSend(app->usb_rx, data, len, 0);
    if (sent != len) {
        ESP_LOGW(TAG, "usb rx buffer full, dropped %u of %u bytes", (unsigned)(len - sent),
                 (unsigned)len);
    }
}

static fw_err_t on_usb_reply(void *ctx, const uint8_t *data, size_t len) {
    app_ctx_t *app              = (app_ctx_t *)ctx;
    const usb_cdc_err_t usb_err = usb_cdc_write(&app->usb, data, len);

    if (usb_err != USB_CDC_OK) {
        /* Logged once, here, by the layer that decides what to do about it
         * (R-LOG-04). Nothing is retried: a host that stopped reading will
         * send its command again, and the answer is cheap to rebuild. */
        ESP_LOGW(TAG, "usb reply (%u bytes): %s", (unsigned)len, usb_cdc_err_str(usb_err));
        return (usb_err == USB_CDC_ERR_TIMEOUT) ? FW_ERR_TIMEOUT : FW_ERR_IO;
    }
    return FW_OK;
}

/* What UPG_BEGIN has to know before it erases a slot: whether the scheduled
 * HTTP update is already writing the same one. Asked through a callback
 * because the dispatcher lives below this layer and may not include
 * updater.h. */
static bool is_slot_write_busy(void *ctx) {
    const app_ctx_t *app  = (const app_ctx_t *)ctx;
    updater_state_t state = UPDATER_STATE_IDLE;

    if (updater_state_get(&app->updater, &state) != FW_OK) {
        /* Unreadable means unknown, and unknown has to read as busy: refusing
         * a transfer is recoverable, two writers on one slot is not. */
        return true;
    }
    return (state == UPDATER_STATE_CHECKING) || (state == UPDATER_STATE_DOWNLOADING);
}

/* Parsing and dispatch happen here, never in the USB callback: a handler may
 * hold the channel for a flash write, and doing that inside the callback would
 * stop the FIFO being drained at all. */
static void usb_dispatch_task(void *arg) {
    app_ctx_t *ctx = (app_ctx_t *)arg;

    for (;;) {
        uint8_t chunk[64];
        const size_t got = xStreamBufferReceive(ctx->usb_rx, chunk, sizeof(chunk), portMAX_DELAY);

        for (size_t i = 0; i < got; ++i) {
            protocol_req_t req;
            if (protocol_parser_feed(&ctx->parser, chunk[i], &req)) {
                const fw_err_t err = command_on_frame(&ctx->command, &req);
                if (err != FW_OK) {
                    ESP_LOGW(TAG, "command 0x%04X: %s", (unsigned)req.command, fw_err_str(err));
                }
            }
        }
    }
}

/* esp_timer_get_time() is monotonic from boot and keeps running in light
 * sleep, which is what the update schedule needs. */
static uint32_t now_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/*** end of file ***/
