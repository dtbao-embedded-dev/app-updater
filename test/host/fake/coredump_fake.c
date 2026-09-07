/**
 * @file    coredump_fake.c
 * @date    2026-09-07
 * @brief   Host fake of `driver/coredump`: an in-memory core dump partition
 *          the tests can fill, damage, and prove was erased.
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

/* ------------------------------ Includes ------------------------------- */

#include "coredump_fake.h"

#include <stdio.h>
#include <string.h>

/* ----------------------------- Static data ----------------------------- */

static coredump_state_t s_state;
static uint8_t s_bytes[COREDUMP_FAKE_MAX];
static uint32_t s_len;
static bool s_has_reason;
static char s_reason[COREDUMP_REASON_MAX];
static coredump_err_t s_fail_info;
static coredump_err_t s_fail_read;
static coredump_err_t s_fail_erase;
static uint32_t s_read_count;
static uint32_t s_erase_count;
static bool s_erased;

/* ------------------------ Test control surface ------------------------- */

void coredump_fake_reset(void) {
    s_state = COREDUMP_ABSENT;
    memset(s_bytes, 0, sizeof(s_bytes));
    s_len        = 0U;
    s_has_reason = false;
    memset(s_reason, 0, sizeof(s_reason));
    s_fail_info   = COREDUMP_OK;
    s_fail_read   = COREDUMP_OK;
    s_fail_erase  = COREDUMP_OK;
    s_read_count  = 0U;
    s_erase_count = 0U;
    s_erased      = false;
}

void coredump_fake_set_dump(coredump_state_t state, const uint8_t *bytes, uint32_t len) {
    s_state = state;
    memset(s_bytes, 0, sizeof(s_bytes));

    s_len = (len > COREDUMP_FAKE_MAX) ? COREDUMP_FAKE_MAX : len;
    if (bytes != NULL) {
        memcpy(s_bytes, bytes, (size_t)s_len);
    }
    s_erased = false;
}

void coredump_fake_set_reason(const char *reason) {
    if (reason == NULL) {
        s_has_reason = false;
        memset(s_reason, 0, sizeof(s_reason));
        return;
    }

    s_has_reason = true;
    (void)snprintf(s_reason, sizeof(s_reason), "%s", reason);
}

void coredump_fake_fail_info(coredump_err_t err) { s_fail_info = err; }
void coredump_fake_fail_read(coredump_err_t err) { s_fail_read = err; }
void coredump_fake_fail_erase(coredump_err_t err) { s_fail_erase = err; }

uint32_t coredump_fake_read_count(void) { return s_read_count; }
uint32_t coredump_fake_erase_count(void) { return s_erase_count; }
bool coredump_fake_is_erased(void) { return s_erased; }

/* ------------------------- The faked contract -------------------------- */

const char *coredump_err_str(coredump_err_t err) {
    switch (err) {
        case COREDUMP_OK:
            return "COREDUMP_OK";
        case COREDUMP_ERR_PARAM:
            return "COREDUMP_ERR_PARAM";
        case COREDUMP_ERR_NOT_FOUND:
            return "COREDUMP_ERR_NOT_FOUND";
        case COREDUMP_ERR_IO:
            return "COREDUMP_ERR_IO";
        default:
            return "COREDUMP_ERR_UNKNOWN";
    }
}

coredump_err_t coredump_info_get(coredump_state_t *out_state, uint32_t *out_size) {
    if ((out_state == NULL) || (out_size == NULL)) {
        return COREDUMP_ERR_PARAM;
    }
    if (s_fail_info != COREDUMP_OK) {
        return s_fail_info;
    }

    *out_state = s_state;
    *out_size  = (s_state == COREDUMP_ABSENT) ? 0U : s_len;
    return COREDUMP_OK;
}

coredump_err_t coredump_read(uint32_t offset, void *out, uint32_t len) {
    if ((out == NULL) || (len == 0U)) {
        return COREDUMP_ERR_PARAM;
    }

    s_read_count += 1U;

    if (s_fail_read != COREDUMP_OK) {
        return s_fail_read;
    }
    if (s_state == COREDUMP_ABSENT) {
        return COREDUMP_ERR_NOT_FOUND;
    }

    /* The real driver bounds a read to the STORED length, not the partition
     * size, so this one does too - past the dump there is only padding. */
    if ((len > s_len) || (offset > (s_len - len))) {
        return COREDUMP_ERR_PARAM;
    }

    memcpy(out, &s_bytes[offset], (size_t)len);
    return COREDUMP_OK;
}

coredump_err_t coredump_erase(void) {
    s_erase_count += 1U;

    if (s_fail_erase != COREDUMP_OK) {
        return s_fail_erase;
    }

    /* Erasing a blank partition succeeds, exactly as on target. */
    s_state = COREDUMP_ABSENT;
    s_len   = 0U;
    memset(s_bytes, 0, sizeof(s_bytes));
    s_erased = true;
    return COREDUMP_OK;
}

coredump_err_t coredump_reason_get(char *out, size_t cap) {
    if ((out == NULL) || (cap == 0U)) {
        return COREDUMP_ERR_PARAM;
    }

    out[0] = '\0';

    if ((s_state == COREDUMP_ABSENT) || !s_has_reason) {
        return COREDUMP_ERR_NOT_FOUND;
    }

    (void)snprintf(out, cap, "%s", s_reason);
    return COREDUMP_OK;
}

/*** end of file ***/
