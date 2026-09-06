/**
 * @file    ota_http.c
 * @author  dtbao
 * @date    2026-09-06
 * @brief   Fetches a firmware image over HTTPS and hands it to the caller one
 *          chunk at a time.
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

/* ------------------------------ Includes ------------------------------- */

#include "ota_http.h"

#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_log.h"

#include <string.h>

/* --------------------------- Private macros ---------------------------- */

/** Per-read budget when the caller does not pick one. */
#define OTA_HTTP_DEFAULT_TIMEOUT_MS 10000U

/** HTTP status the fetch accepts; anything else is FW_ERR_IO. */
#define OTA_HTTP_STATUS_OK 200

/* ---------------------------- Private types ---------------------------- */

/* ----------------------------- Static data ----------------------------- */

static const char *TAG = "ota_http";

/* --------------------- Private function prototypes --------------------- */

static fw_err_t drain_body(ota_http_t *cli);
static fw_err_t from_esp_err(esp_err_t err);

/* -------------------------- Public functions --------------------------- */

ota_http_cfg_t ota_http_cfg_default(void) {
    const ota_http_cfg_t cfg = {
        .url        = NULL,
        .cert_pem   = NULL,
        .on_chunk   = NULL,
        .chunk_ctx  = NULL,
        .timeout_ms = OTA_HTTP_DEFAULT_TIMEOUT_MS,
    };
    return cfg;
}

fw_err_t ota_http_init(ota_http_t *cli, const ota_http_cfg_t *cfg) {
    if ((cli == NULL) || (cfg == NULL) || (cfg->url == NULL) || (cfg->on_chunk == NULL)) {
        return FW_ERR_PARAM;
    }
    /* A zero timeout would mean "non-blocking" (R-LFC-08), which this transport
     * cannot honour — reject it rather than silently blocking forever. */
    if (cfg->timeout_ms == 0U) {
        return FW_ERR_PARAM;
    }
    if (cli->is_init) {
        return FW_ERR_STATE;
    }

    memset(cli, 0, sizeof(*cli));
    cli->cfg = *cfg;

    const esp_http_client_config_t http_cfg = {
        .url               = cfg->url,
        .cert_pem          = cfg->cert_pem,
        .timeout_ms        = (int)cfg->timeout_ms,
        .keep_alive_enable = true,
    };

    cli->client = esp_http_client_init(&http_cfg);
    if (cli->client == NULL) {
        ESP_LOGE(TAG, "client init failed for url len=%u", (unsigned)strlen(cfg->url));
        return FW_ERR_NO_MEM;
    }

    cli->is_init = true;
    return FW_OK;
}

fw_err_t ota_http_deinit(ota_http_t *cli) {
    if (cli == NULL) {
        return FW_ERR_PARAM;
    }

    /* Repeatable and safe on a half-built instance (R-LFC-04). */
    if (cli->client != NULL) {
        (void)esp_http_client_cleanup((esp_http_client_handle_t)cli->client);
    }
    memset(cli, 0, sizeof(*cli));
    return FW_OK;
}

fw_err_t ota_http_fetch(ota_http_t *cli) {
    if (cli == NULL) {
        return FW_ERR_PARAM;
    }
    if (!cli->is_init) {
        return FW_ERR_STATE;
    }
    if (cli->is_running) {
        return FW_ERR_STATE;
    }

    esp_http_client_handle_t handle = (esp_http_client_handle_t)cli->client;

    const esp_err_t open_err = esp_http_client_open(handle, 0);
    if (open_err != ESP_OK) {
        ESP_LOGE(TAG, "open failed: esp_err=0x%x", (unsigned)open_err);
        return from_esp_err(open_err);
    }

    cli->is_running     = true;
    cli->received_bytes = 0U;

    fw_err_t result = FW_OK;

    const int64_t content_len = esp_http_client_fetch_headers(handle);
    const int status          = esp_http_client_get_status_code(handle);
    if (content_len < 0) {
        ESP_LOGE(TAG, "header fetch failed: content_len=%lld", (long long)content_len);
        result = FW_ERR_IO;
    } else if (status != OTA_HTTP_STATUS_OK) {
        ESP_LOGE(TAG, "unexpected status: got=%d want=%d len=%lld", status, OTA_HTTP_STATUS_OK,
                 (long long)content_len);
        result = FW_ERR_IO;
    } else {
        result = drain_body(cli);
    }

    (void)esp_http_client_close(handle);
    cli->is_running = false;
    return result;
}

fw_err_t ota_http_progress_get(const ota_http_t *cli, uint32_t *out_bytes) {
    if ((cli == NULL) || (out_bytes == NULL)) {
        return FW_ERR_PARAM;
    }
    if (!cli->is_init) {
        return FW_ERR_STATE;
    }

    *out_bytes = cli->received_bytes;
    return FW_OK;
}

/* -------------------------- Private functions -------------------------- */

/* Arguments are checked at the public boundary (R-SRC-06); helpers assume it. */

static fw_err_t drain_body(ota_http_t *cli) {
    esp_http_client_handle_t handle = (esp_http_client_handle_t)cli->client;
    uint8_t chunk[OTA_HTTP_CHUNK_BYTES];

    for (;;) {
        const int read = esp_http_client_read(handle, (char *)chunk, (int)sizeof(chunk));
        if (read < 0) {
            ESP_LOGE(TAG, "read failed after %lu bytes", (unsigned long)cli->received_bytes);
            return FW_ERR_IO;
        }
        if (read == 0) {
            /* Zero means the transport is done, or it timed out with nothing to
             * give. esp_http_client_is_complete_data_received() is what tells
             * the two apart — a truncated image must not report success. */
            if (!esp_http_client_is_complete_data_received(handle)) {
                ESP_LOGE(TAG, "body truncated at %lu bytes", (unsigned long)cli->received_bytes);
                return FW_ERR_TIMEOUT;
            }
            return FW_OK;
        }

        const fw_err_t err = cli->cfg.on_chunk(cli->cfg.chunk_ctx, chunk, (size_t)read);
        if (err != FW_OK) {
            ESP_LOGW(TAG, "consumer aborted at %lu bytes: %s", (unsigned long)cli->received_bytes,
                     fw_err_str(err));
            return err;
        }
        cli->received_bytes += (uint32_t)read;
    }
}

/* R-ERR-04: the vendor code stops here, logged at the call site above. */
static fw_err_t from_esp_err(esp_err_t err) {
    switch (err) {
        case ESP_OK:
            return FW_OK;
        case ESP_ERR_INVALID_ARG:
            return FW_ERR_PARAM;
        case ESP_ERR_INVALID_STATE:
            return FW_ERR_STATE;
        case ESP_ERR_TIMEOUT:
            return FW_ERR_TIMEOUT;
        case ESP_ERR_NO_MEM:
            return FW_ERR_NO_MEM;
        default:
            return FW_ERR_IO;
    }
}

/*** end of file ***/
