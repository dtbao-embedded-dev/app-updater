/**
 * @file    app.c
 * @author  dtbao
 * @date    2026-09-06
 * @brief   Brings every module up in order and runs the update cycle.
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

/* ------------------------------ Includes ------------------------------- */

#include "app.h"

#include "app_priv.h"
#include "bsp.h"
#include "storage.h"
#include "updater.h"

#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

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

/* ---------------------------- Private types ---------------------------- */

/** Everything the application owns, in one struct passed down at init
 *  (R-CFG-09). No module reaches for a global of its own. */
typedef struct {
    bsp_t bsp;
    storage_t storage;
    updater_t updater;
    storage_record_t record;
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
    (void)esp_read_mac(mac, ESP_MAC_WIFI_STA);

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
    esp_err_t nvs_err = nvs_flash_init();
    if ((nvs_err == ESP_ERR_NVS_NO_FREE_PAGES) || (nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND)) {
        ESP_LOGW(TAG, "nvs unusable (esp_err=0x%x), erasing", (unsigned)nvs_err);
        if (nvs_flash_erase() != ESP_OK) {
            return FW_ERR_IO;
        }
        nvs_err = nvs_flash_init();
    }
    if (nvs_err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init failed: esp_err=0x%x", (unsigned)nvs_err);
        return FW_ERR_IO;
    }

    const storage_cfg_t cfg = storage_cfg_default();
    const fw_err_t err      = storage_init(&ctx->storage, &cfg);
    if (err != FW_OK) {
        return err;
    }

    /* A missing or damaged record is not a failure: the device boots on the
     * compiled-in defaults and says so (R-CFG-02, R-CFG-03). */
    const fw_err_t load_err = storage_record_load(&ctx->storage, &ctx->record);
    if (load_err != FW_OK) {
        ESP_LOGW(TAG, "record unusable (%s), using defaults", fw_err_str(load_err));
        ctx->record = storage_record_default();
    }
    return FW_OK;
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
    updater_cfg_t cfg     = updater_cfg_default();
    cfg.check_interval_ms = ctx->record.check_interval_ms;
    cfg.on_state          = on_updater_state;
    cfg.on_state_ctx      = ctx;

    const fw_err_t err = updater_init(&ctx->updater, &cfg);
    if (err != FW_OK) {
        return err;
    }
    return updater_start(&ctx->updater, now_ms());
}

/* R-VER-08: a new image gets exactly one boot to prove itself. Without this
 * call the bootloader reverts on the next reset, which is the safe default but
 * makes every good update look like a failed one. */
static void confirm_or_roll_back(void) {
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;

    if (esp_ota_get_state_partition(running, &state) != ESP_OK) {
        return;
    }
    if (state != ESP_OTA_IMG_PENDING_VERIFY) {
        return;
    }

    /* SPEC-DEVIATION(R-VER-08): this confirms without running a self-test.
     * TODO(dtbao): run the real diagnostic here before the first field build.
     * An image that can confirm itself while broken defeats the rollback this
     * firmware exists to provide. */
    ESP_LOGW(TAG, "pending verify: confirming without a self-test (TODO)");
    (void)esp_ota_mark_app_valid_cancel_rollback();
}

static void on_updater_state(void *ctx, updater_state_t state) {
    app_ctx_t *app = (app_ctx_t *)ctx;

    const bool is_busy  = (state == UPDATER_STATE_CHECKING) || (state == UPDATER_STATE_DOWNLOADING);
    const bsp_err_t err = bsp_led_status_set(&app->bsp, is_busy);
    if ((err != BSP_OK) && (err != BSP_ERR_NO_PIN)) {
        ESP_LOGW(TAG, "status led: %s", bsp_err_str(err));
    }
}

/* esp_timer_get_time() is monotonic from boot and keeps running in light
 * sleep, which is what the update schedule needs. */
static uint32_t now_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/*** end of file ***/
