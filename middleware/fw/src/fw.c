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

/* -------------------------- Private functions -------------------------- */

/*** end of file ***/
