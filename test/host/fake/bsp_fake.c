/**
 * @file    bsp_fake.c
 * @date    2026-09-07
 * @brief   Host fake of the instance-free half of `driver/bsp`: the eFuse MAC
 *          and the reset.
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

/* ------------------------------ Includes ------------------------------- */

#include "bsp_fake.h"

#include <string.h>

/* --------------------------- Private macros ---------------------------- */

#define MAC_KIND_COUNT 2

/* ----------------------------- Static data ----------------------------- */

static uint8_t s_mac[MAC_KIND_COUNT][BSP_MAC_LEN];
static bsp_err_t s_fail_mac_get;
static uint32_t s_restart_count;
static uint32_t s_grace_total_ms;

/* ------------------------- The control surface ------------------------- */

void bsp_fake_reset(void) {
    memset(s_mac, 0, sizeof(s_mac));
    s_mac[BSP_MAC_WIFI][0] = 0xAAU;
    s_mac[BSP_MAC_WIFI][5] = 0x01U;
    s_mac[BSP_MAC_BLE][0]  = 0xAAU;
    s_mac[BSP_MAC_BLE][5]  = 0x02U;

    s_fail_mac_get   = BSP_OK;
    s_restart_count  = 0U;
    s_grace_total_ms = 0U;
}

void bsp_fake_set_mac(bsp_mac_kind_t kind, const uint8_t *mac) {
    if ((mac != NULL) && ((int)kind < MAC_KIND_COUNT)) {
        memcpy(s_mac[kind], mac, BSP_MAC_LEN);
    }
}

void bsp_fake_fail_mac_get(bsp_err_t err) {
    s_fail_mac_get = err;
}

uint32_t bsp_fake_restart_count(void) {
    return s_restart_count;
}

uint32_t bsp_fake_grace_total_ms(void) {
    return s_grace_total_ms;
}

/* --------------------------- The fake proper --------------------------- */

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
        default:
            return "BSP_ERR_UNKNOWN";
    }
}

bsp_err_t bsp_mac_get(bsp_mac_kind_t kind, uint8_t *out) {
    if ((out == NULL) || ((int)kind >= MAC_KIND_COUNT)) {
        return BSP_ERR_PARAM;
    }
    if (s_fail_mac_get != BSP_OK) {
        return s_fail_mac_get;
    }

    memcpy(out, s_mac[kind], BSP_MAC_LEN);
    return BSP_OK;
}

void bsp_restart(uint32_t grace_ms) {
    /* The target never returns from this. The fake does, and counts both the
     * reset and the grace period - which is what proves RESTART_APP replied
     * before it reset, and gave the reply time to leave. */
    s_grace_total_ms += grace_ms;
    s_restart_count += 1U;
}

/*** end of file ***/
