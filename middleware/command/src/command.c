/**
 * @file    command.c
 * @date    2026-09-06
 * @brief   Dispatches one decoded USB command frame to the handler that
 *          serves it, and answers everything else with a status the host can
 *          act on.
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

/* ------------------------------ Includes ------------------------------- */

#include "command.h"

#include "command_priv.h"

#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

/* --------------------------- Private macros ---------------------------- */

/** Biggest response payload any handler here produces, VERSION being it. */
#define PAYLOAD_MAX PROTOCOL_VERSION_LEN

/* ----------------------------- Static data ----------------------------- */

static const char *TAG = "command";

/* --------------------- Private function prototypes --------------------- */

static protocol_status_t serve(command_t *cmd, const protocol_req_t *req, uint8_t *payload,
                               uint32_t *payload_len, const uint8_t **echo);
static protocol_status_t handle_restart_app(command_t *cmd);
static protocol_status_t handle_set_boot_slot(const protocol_req_t *req);
static protocol_status_t handle_get_version(uint8_t *payload, uint32_t *payload_len);
static protocol_status_t handle_get_boot_slot(uint8_t *payload, uint32_t *payload_len);
static protocol_status_t handle_get_mac(esp_mac_type_t type, uint8_t *payload,
                                        uint32_t *payload_len);
static fw_err_t reply(command_t *cmd, uint32_t command, protocol_status_t status,
                      const uint8_t *payload, uint32_t payload_len);
static void copy_field(uint8_t *dst, uint32_t cap, const char *src);

/* -------------------------- Public functions --------------------------- */

fw_err_t command_init(command_t *cmd, const command_cfg_t *cfg) {
    if ((cmd == NULL) || (cfg == NULL) || (cfg->on_reply == NULL)) {
        return FW_ERR_PARAM;
    }
    if (cmd->is_init) {
        return FW_ERR_STATE;
    }

    memset(cmd, 0, sizeof(*cmd));
    cmd->cfg     = *cfg;
    cmd->is_init = true;
    return FW_OK;
}

fw_err_t command_deinit(command_t *cmd) {
    if (cmd == NULL) {
        return FW_ERR_PARAM;
    }

    /* Repeatable and safe on a half-built instance (R-LFC-04). An open
     * transfer is thrown away rather than finalised: a slot half written is a
     * slot nothing should boot, and only UPG_END makes one bootable. */
    if (cmd->is_init && cmd->upgrade.is_open) {
        (void)esp_ota_abort((esp_ota_handle_t)cmd->upgrade.ota_handle);
    }
    memset(cmd, 0, sizeof(*cmd));
    return FW_OK;
}

fw_err_t command_on_frame(command_t *cmd, const protocol_req_t *req) {
    if ((cmd == NULL) || (req == NULL)) {
        return FW_ERR_PARAM;
    }
    if (!cmd->is_init) {
        return FW_ERR_STATE;
    }

    /* The map first, because -2 and -7 are decided by the opcode alone. */
    const protocol_cmd_info_t *info = protocol_cmd_lookup(req->command);
    if (info == NULL) {
        return reply(cmd, req->command, PROTOCOL_ERR_BAD_CMD, NULL, 0U);
    }
    if (info->kind != PROTOCOL_CMD_SERVED) {
        return reply(cmd, req->command, PROTOCOL_ERR_UNSUPPORTED, NULL, 0U);
    }

    /* Then the width, before any value is looked at, so a host is told which
     * half of its request to fix (R5). A handler never sees a wrong length. */
    if ((info->req_len != PROTOCOL_LEN_ANY) && (req->len != info->req_len)) {
        return reply(cmd, req->command, PROTOCOL_ERR_BAD_LEN, NULL, 0U);
    }

    uint8_t payload[PAYLOAD_MAX];
    uint32_t payload_len = 0U;
    const uint8_t *echo  = NULL;
    memset(payload, 0, sizeof(payload));

    const protocol_status_t status = serve(cmd, req, payload, &payload_len, &echo);

    /* PING answers with the request's own bytes, which are far too big to copy
     * through the small payload buffer above - so a handler may point `echo`
     * at them instead and the frame builder copies once, straight to the wire. */
    return reply(cmd, req->command, status, (echo != NULL) ? echo : payload, payload_len);
}

/* -------------------------- Private functions -------------------------- */

/* Arguments are checked at the public boundary (R-SRC-06); helpers assume it. */

/* One flat switch over the ten served opcodes rather than the two-level
 * range-then-item switch the source spec describes. At ten cases the extra
 * level is ceremony: the compiler builds the same jump table either way, and a
 * reader looking up `0x0202` finds it in one place. */
static protocol_status_t serve(command_t *cmd, const protocol_req_t *req, uint8_t *payload,
                               uint32_t *payload_len, const uint8_t **echo) {
    switch (req->command) {
        case PROTOCOL_CMD_PING:
            /* The one place the source spec contradicts itself: it says PING
             * echoes up to PROTOCOL_MAX_DATA bytes AND that the reply carries
             * 4 + REQ.LENGTH - which past 32768 asks for a frame bigger than
             * the cap. Refusing the length beats truncating the echo. */
            if (req->len > (PROTOCOL_MAX_DATA - PROTOCOL_STATUS_LEN)) {
                return PROTOCOL_ERR_BAD_LEN;
            }
            *echo        = req->data;
            *payload_len = req->len;
            return PROTOCOL_OK;

        case PROTOCOL_CMD_RESTART_APP:
            return handle_restart_app(cmd);

        case PROTOCOL_CMD_SET_BOOT_SLOT:
            return handle_set_boot_slot(req);

        case PROTOCOL_CMD_GET_VERSION:
            return handle_get_version(payload, payload_len);

        case PROTOCOL_CMD_GET_BOOT_SLOT:
            return handle_get_boot_slot(payload, payload_len);

        case PROTOCOL_CMD_GET_WIFI_MAC:
            return handle_get_mac(ESP_MAC_WIFI_STA, payload, payload_len);

        case PROTOCOL_CMD_GET_BLE_MAC:
            return handle_get_mac(ESP_MAC_BT, payload, payload_len);

        case PROTOCOL_CMD_UPG_BEGIN:
            return command_upgrade_begin(cmd, req);

        case PROTOCOL_CMD_UPG_WRITE:
            return command_upgrade_write(cmd, req);

        case PROTOCOL_CMD_UPG_END:
            return command_upgrade_end(cmd);

        default:
            /* Unreachable: the map already said this opcode is served, so a
             * value arriving here means a row was added without a case. */
            ESP_LOGE(TAG, "served opcode 0x%04X has no handler", (unsigned)req->command);
            return PROTOCOL_ERR_UNSUPPORTED;
    }
}

/* The reply has to be on the wire before the reset, or a host cannot tell
 * "restarting" from "the cable fell out". Flushed is not the same as read - the
 * host still has to be scheduled - so the grace period covers that gap. */
static protocol_status_t handle_restart_app(command_t *cmd) {
    (void)reply(cmd, PROTOCOL_CMD_RESTART_APP, PROTOCOL_OK, NULL, 0U);

    ESP_LOGI(TAG, "restart requested over usb, resetting in %ums",
             (unsigned)COMMAND_RESTART_GRACE_MS);
    vTaskDelay(pdMS_TO_TICKS(COMMAND_RESTART_GRACE_MS));
    esp_restart();

    /* Never reached on target. On the host the fake counts the reset and
     * returns, and the caller must not send a second reply - which is what the
     * PROTOCOL_ERR_CRC below says: it is the one status the wire never
     * carries, so command_on_frame() uses it as "already answered". */
    return PROTOCOL_ERR_CRC;
}

/* Arms the slot for the NEXT boot. Not symmetric with the Get on purpose: this
 * writes otadata, that one reports what is running now. */
static protocol_status_t handle_set_boot_slot(const protocol_req_t *req) {
    const uint8_t slot = req->data[0];

    if ((slot != PROTOCOL_SLOT_UPDATER) && (slot != PROTOCOL_SLOT_FIRMWARE)) {
        return PROTOCOL_ERR_BAD_ARG;
    }

    const esp_partition_t *part = command_slot_partition(slot);
    if (part == NULL) {
        return PROTOCOL_ERR_BAD_ARG;
    }

    const esp_err_t err = esp_ota_set_boot_partition(part);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "arm slot %u failed: esp_err=0x%x", (unsigned)slot, (unsigned)err);
        /* A slot whose image does not validate is a state a host can fix by
         * transferring one, so it is retryable (-5) rather than broken (-6). */
        return PROTOCOL_ERR_STATE;
    }
    return PROTOCOL_OK;
}

/* `[0:16]` is the image running now - the updater, which the source spec calls
 * BL2 - and `[16:32]` is the app_firmware slot. A slot that was never written
 * reads back as sixteen zero bytes with PROTOCOL_OK: "unset" is an answer, not
 * a failure, so a host needs no second command to tell them apart. */
static protocol_status_t handle_get_version(uint8_t *payload, uint32_t *payload_len) {
    const esp_app_desc_t *self = esp_app_get_description();
    if (self != NULL) {
        copy_field(&payload[0], PROTOCOL_VERSION_FIELD_LEN, self->version);
    }

    const esp_partition_t *fw = command_slot_partition(PROTOCOL_SLOT_FIRMWARE);
    esp_app_desc_t desc;
    if ((fw != NULL) && (esp_ota_get_partition_description(fw, &desc) == ESP_OK)) {
        copy_field(&payload[PROTOCOL_VERSION_FIELD_LEN], PROTOCOL_VERSION_FIELD_LEN, desc.version);
    }

    *payload_len = PROTOCOL_VERSION_LEN;
    return PROTOCOL_OK;
}

static protocol_status_t handle_get_boot_slot(uint8_t *payload, uint32_t *payload_len) {
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running == NULL) {
        return PROTOCOL_ERR_HW;
    }

    if (running->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0) {
        payload[0] = (uint8_t)PROTOCOL_SLOT_UPDATER;
    } else if (running->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1) {
        payload[0] = (uint8_t)PROTOCOL_SLOT_FIRMWARE;
    } else {
        /* Neither OTA slot: the bootloader started something this map cannot
         * name, which no partitions.csv in this repo produces. */
        ESP_LOGE(TAG, "running partition subtype 0x%02X is neither ota slot",
                 (unsigned)running->subtype);
        return PROTOCOL_ERR_HW;
    }

    *payload_len = 1U;
    return PROTOCOL_OK;
}

/* Both MACs come from eFuse, so they answer with no radio brought up and no
 * BLE stack linked in - which is why this product serves them while every ATE
 * hardware check answers -7. */
static protocol_status_t handle_get_mac(esp_mac_type_t type, uint8_t *payload,
                                        uint32_t *payload_len) {
    const esp_err_t err = esp_read_mac(payload, type);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "read mac type=%d failed: esp_err=0x%x", (int)type, (unsigned)err);
        return PROTOCOL_ERR_HW;
    }

    *payload_len = PROTOCOL_MAC_LEN;
    return PROTOCOL_OK;
}

/* Builds the frame into the instance's own buffer and hands it to the
 * transport. PROTOCOL_ERR_CRC means "a reply already went out", which is safe
 * to overload because it is the one status the wire never carries. */
static fw_err_t reply(command_t *cmd, uint32_t command, protocol_status_t status,
                      const uint8_t *payload, uint32_t payload_len) {
    if (status == PROTOCOL_ERR_CRC) {
        return FW_OK;
    }

    const uint32_t len =
        protocol_rsp_build(cmd->frame, (uint32_t)sizeof(cmd->frame), command, status,
                           (payload_len > 0U) ? payload : NULL, payload_len);
    if (len == 0U) {
        /* Only reachable if a handler asked for a payload that cannot fit,
         * which every one of them checks first. Say so rather than sending a
         * truncated frame the host has to resync past. */
        ESP_LOGE(TAG, "cannot build reply for 0x%04X, payload=%u", (unsigned)command,
                 (unsigned)payload_len);
        return FW_ERR_NO_SPACE;
    }

    return cmd->cfg.on_reply(cmd->cfg.reply_ctx, cmd->frame, (size_t)len);
}

/* Bounded copy into a fixed field the caller has already zeroed, so the NUL is
 * already in place. Hand-rolled rather than strncpy to keep the truncation
 * explicit instead of arguing with -Wstringop-truncation about it. */
static void copy_field(uint8_t *dst, uint32_t cap, const char *src) {
    if ((src == NULL) || (cap == 0U)) {
        return;
    }
    for (uint32_t i = 0; (i < (cap - 1U)) && (src[i] != '\0'); ++i) {
        dst[i] = (uint8_t)src[i];
    }
}

/* Slot 1 is app_firmware (ota_1) and slot 0 is app_updater (ota_0), the same
 * encoding BOOT_SLOT and UPG_BEGIN's `target` share - two encodings for one
 * pair of slots is a bug waiting to happen. */
const esp_partition_t *command_slot_partition(uint8_t slot) {
    const esp_partition_subtype_t subtype = (slot == PROTOCOL_SLOT_FIRMWARE)
                                                ? ESP_PARTITION_SUBTYPE_APP_OTA_1
                                                : ESP_PARTITION_SUBTYPE_APP_OTA_0;

    return esp_partition_find_first(ESP_PARTITION_TYPE_APP, subtype, NULL);
}

/*** end of file ***/
