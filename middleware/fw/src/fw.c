/**
 * @file    fw.c
 * @date    2026-09-06
 * @brief   Project-wide status code returned by every application and
 *          middleware function (R-ERR-08).
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

/* ------------------------------ Includes ------------------------------- */

#include "fw.h"

/* --------------------------- Private macros ---------------------------- */

/* ---------------------------- Private types ---------------------------- */

/* ----------------------------- Static data ----------------------------- */

/* Four bits per step: 64 bytes of table against the 1 KB a byte-wide table
 * costs, for two lookups per byte instead of eight shifts. The image this
 * firmware writes is up to 13.875 MB and every byte of it passes through
 * here, which is what makes the table worth its flash at all. */
static const uint32_t s_crc_nibble[16] = {
    0x00000000U, 0x1DB71064U, 0x3B6E20C8U, 0x26D930ACU, 0x76DC4190U, 0x6B6B51F4U,
    0x4DB26158U, 0x5005713CU, 0xEDB88320U, 0xF00F9344U, 0xD6D6A3E8U, 0xCB61B38CU,
    0x9B64C2B0U, 0x86D3D2D4U, 0xA00AE278U, 0xBDBDF21CU,
};

/* --------------------- Private function prototypes --------------------- */

/* -------------------------- Public functions --------------------------- */

const char *fw_err_str(fw_err_t err) {
    /* No `default` label: -Wswitch-enum (R-BLD-01) then fails the build when a
     * code is added here without a name, which is the whole point of R-ERR-06.
     * The fallthrough after the switch covers a value cast in from outside. */
    switch (err) {
        case FW_OK:
            return "FW_OK";
        case FW_ERR_PARAM:
            return "FW_ERR_PARAM";
        case FW_ERR_STATE:
            return "FW_ERR_STATE";
        case FW_ERR_TIMEOUT:
            return "FW_ERR_TIMEOUT";
        case FW_ERR_NO_SPACE:
            return "FW_ERR_NO_SPACE";
        case FW_ERR_UNSUPPORTED:
            return "FW_ERR_UNSUPPORTED";
        case FW_ERR_NOT_FOUND:
            return "FW_ERR_NOT_FOUND";
        case FW_ERR_IO:
            return "FW_ERR_IO";
        case FW_ERR_CRC:
            return "FW_ERR_CRC";
        case FW_ERR_NO_MEM:
            return "FW_ERR_NO_MEM";
    }

    return "FW_ERR_UNKNOWN";
}

uint32_t fw_crc32_le(uint32_t seed, const void *data, size_t len) {
    const uint8_t *in = (const uint8_t *)data;

    /* The ROM routine this replaced seeds with ~crc and returns ~crc, so
     * passing 0 yields the standard init/xorout of 0xFFFFFFFF. Reproduced
     * exactly, including the double inversion, so a chained call chains the
     * same way and every CRC already stored in flash still checks out. */
    uint32_t value = ~seed;

    for (size_t i = 0; i < len; ++i) {
        value ^= (uint32_t)in[i];
        value = (value >> 4) ^ s_crc_nibble[value & 0x0FU];
        value = (value >> 4) ^ s_crc_nibble[value & 0x0FU];
    }

    return ~value;
}

/* -------------------------- Private functions -------------------------- */

/*** end of file ***/
