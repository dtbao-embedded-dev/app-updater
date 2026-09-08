/**
 * @file    coredump_fake.h
 * @date    2026-09-07
 * @brief   Test-side control surface for the host fake of `driver/coredump`.
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

#ifndef HOST_FAKE_COREDUMP_FAKE_H
#define HOST_FAKE_COREDUMP_FAKE_H

/* ------------------------------ Includes ------------------------------- */

#include "coredump.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------- Constants & macros ------------------------- */

/**
 * Largest dump this fake can hold. Bigger than one `DUMP_READ` chunk on
 * purpose, so a test can prove the handler reads across chunk boundaries
 * instead of only ever answering the first one.
 */
#define COREDUMP_FAKE_MAX 8192U

/* ------------------------ Public function prototypes ------------------- */

/*
 * A fake of the driver, not of the vendor SDK. The module under test calls
 * `coredump_*` and this file answers, so the test drives the same contract the
 * target implementation has to satisfy — a stub of `esp_core_dump.h` would
 * instead pin the test to one platform, which is the defect the driver exists
 * to remove.
 *
 * State lives here rather than as `static` data in the header, because a
 * header's static would give the module under test and the test their own
 * private copy each, and then a test could never see what the module did.
 *
 * Every test calls coredump_fake_reset() first (R-TST-05): this is global
 * state, and a test that inherits the previous test's forced failure is a test
 * that passes for the wrong reason.
 */

/** @brief Puts the fake back to "nothing ever panicked here". */
void coredump_fake_reset(void);

/**
 * @brief   Stores a dump for the module under test to find.
 * @param   state   what `coredump_info_get()` reports
 * @param   bytes   the stored image; NULL sets the state and size with no
 *                  readable content, which is what an ABSENT dump looks like
 * @param   len     length of `bytes`, at most `COREDUMP_FAKE_MAX`
 */
void coredump_fake_set_dump(coredump_state_t state, const uint8_t *bytes, uint32_t len);

/**
 * @brief   Sets the text `coredump_reason_get()` answers with.
 * @param   reason   NULL means the dump carries no reason, so the call
 *                   answers COREDUMP_ERR_NOT_FOUND.
 */
void coredump_fake_set_reason(const char *reason);

/** @brief Forces the matching call to fail; COREDUMP_OK clears it. */
void coredump_fake_fail_info(coredump_err_t err);
void coredump_fake_fail_read(coredump_err_t err);
void coredump_fake_fail_erase(coredump_err_t err);

/** @brief How many times `coredump_read()` was called, to prove chunking. */
uint32_t coredump_fake_read_count(void);

/** @brief How many times `coredump_erase()` was called. */
uint32_t coredump_fake_erase_count(void);

/** @brief True once an erase has actually cleared the stored dump. */
bool coredump_fake_is_erased(void);

#ifdef __cplusplus
}
#endif

#endif /* HOST_FAKE_COREDUMP_FAKE_H */

/*** end of file ***/
