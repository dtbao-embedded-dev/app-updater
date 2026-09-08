/**
 * @file    test_cfg.c
 * @date    2026-09-07
 * @brief   Host tests for the settings record, its validation and its adapter.
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

/* ------------------------------ Includes ------------------------------- */

#include "unity.h"

#include "cfg.h"

#include <string.h>

/* --------------------------- Private macros ---------------------------- */

/** The shipped default, restated here so a silent change to it goes red. */
#define TEST_DEFAULT_CHECK_MS (6U * 60U * 60U * 1000U)

/* ---------------------------- Private types ---------------------------- */

/**
 * @brief   A store that lives in RAM and counts what it was asked to do.
 *
 * The call counters are what let a test assert the negative -- that a setter
 * wrote nothing -- which is the whole point of the adapter seam.
 */
typedef struct {
    cfg_record_t blob;   /**< The "stored" bytes.                          */
    size_t len;          /**< How many of them are valid.                  */
    bool has_blob;       /**< False until something was saved.             */
    fw_err_t force_load; /**< Non-FW_OK makes `load` fail with exactly it.  */
    unsigned load_calls; /**< Times `load` ran.                            */
    unsigned save_calls; /**< Times `save` ran.                            */
} fake_store_t;

/* --------------------- Private function prototypes --------------------- */

static fw_err_t fake_load(void *ctx, void *out, size_t cap, size_t *out_len);
static fw_err_t fake_save(void *ctx, const void *data, size_t len);
static cfg_store_t bind_fake(fake_store_t *fake);
static uint32_t interval_of(const cfg_t *c);
static void fill(char *out, size_t chars);

/* ------------------------ Public function prototypes ------------------- */

/* The runner in test/host/runner.c calls these. Declaring them here is what
 * satisfies -Wmissing-prototypes under the house warning set (R-BLD-01) --
 * a test function is an external symbol like any other. */
void test_cfg_defaults_are_what_an_erased_device_runs_on(void);
void test_cfg_init_falls_back_to_defaults_when_nothing_was_stored(void);
void test_cfg_init_falls_back_to_defaults_when_the_record_is_corrupt(void);
void test_cfg_init_returns_an_adapter_failure_unchanged(void);
void test_cfg_a_saved_value_survives_a_reload(void);
void test_cfg_check_interval_refuses_the_scheduling_horizon(void);
void test_cfg_check_interval_accepts_both_ends_of_its_range(void);
void test_cfg_manifest_url_refuses_a_string_that_does_not_fit(void);
void test_cfg_manifest_url_get_refuses_a_buffer_too_small(void);
void test_cfg_every_accessor_is_err_state_before_init(void);
void test_cfg_rejects_null_arguments(void);
void test_cfg_deinit_is_repeatable_and_safe_half_built(void);
void test_cfg_a_setter_never_writes_to_the_store(void);

/* -------------------------- Public functions --------------------------- */

/* R-CFG-03: a device with erased storage runs on exactly these, so every
 * field has one and none is left for the caller to guess. */
void test_cfg_defaults_are_what_an_erased_device_runs_on(void) {
    const cfg_record_t rec = cfg_record_default();

    TEST_ASSERT_EQUAL_UINT16((uint16_t)CFG_RECORD_VERSION, rec.version);
    TEST_ASSERT_EQUAL_UINT16((uint16_t)sizeof(rec), rec.length);
    TEST_ASSERT_EQUAL_UINT32(TEST_DEFAULT_CHECK_MS, rec.check_interval_ms);
    TEST_ASSERT_EQUAL_UINT32(0U, rec.boot_fail_count);
    TEST_ASSERT_TRUE(rec.manifest_url[0] != '\0');
    TEST_ASSERT_EQUAL_CHAR('\0', rec.manifest_url[CFG_URL_MAX - 1U]);
    TEST_ASSERT_EQUAL_CHAR('\0', rec.last_ok_fw_version[0]);
    TEST_ASSERT_TRUE(rec.crc32 != 0U);
}

/* R-CFG-02: nothing stored is not a failure -- the instance comes up on the
 * compiled-in defaults and the caller is told nothing went wrong. */
void test_cfg_init_falls_back_to_defaults_when_nothing_was_stored(void) {
    fake_store_t fake       = {0};
    const cfg_store_t store = bind_fake(&fake);
    cfg_t c                 = {0};

    TEST_ASSERT_EQUAL_INT(FW_OK, cfg_init(&c, &store));
    TEST_ASSERT_EQUAL_UINT(1U, fake.load_calls);
    TEST_ASSERT_EQUAL_UINT32(TEST_DEFAULT_CHECK_MS, interval_of(&c));
}

/* A record whose CRC does not check out is treated exactly like a missing
 * one: defaults, not a guess at which half of it survived. */
void test_cfg_init_falls_back_to_defaults_when_the_record_is_corrupt(void) {
    fake_store_t fake       = {0};
    const cfg_store_t store = bind_fake(&fake);
    cfg_t writer            = {0};
    cfg_t reader            = {0};

    TEST_ASSERT_EQUAL_INT(FW_OK, cfg_init(&writer, &store));
    TEST_ASSERT_EQUAL_INT(FW_OK, cfg_check_interval_ms_set(&writer, 1234U));
    TEST_ASSERT_EQUAL_INT(FW_OK, cfg_save(&writer));

    /* Prove the round trip first, so the corruption below is the only
     * difference between this and test_cfg_a_saved_value_survives_a_reload. */
    TEST_ASSERT_TRUE(fake.has_blob);
    fake.blob.check_interval_ms ^= 0xFFFFFFFFU;

    TEST_ASSERT_EQUAL_INT(FW_OK, cfg_init(&reader, &store));
    TEST_ASSERT_EQUAL_UINT32(TEST_DEFAULT_CHECK_MS, interval_of(&reader));
}

/* Anything the adapter reports that is not "missing" or "damaged" is the
 * caller's problem, not this module's: it comes back unchanged and the
 * instance stays unusable. */
void test_cfg_init_returns_an_adapter_failure_unchanged(void) {
    fake_store_t fake       = {0};
    const cfg_store_t store = bind_fake(&fake);
    cfg_t c                 = {0};
    uint32_t ms             = 0U;

    fake.force_load = FW_ERR_IO;
    TEST_ASSERT_EQUAL_INT(FW_ERR_IO, cfg_init(&c, &store));
    TEST_ASSERT_EQUAL_INT(FW_ERR_STATE, cfg_check_interval_ms_get(&c, &ms));
}

void test_cfg_a_saved_value_survives_a_reload(void) {
    fake_store_t fake         = {0};
    const cfg_store_t store   = bind_fake(&fake);
    cfg_t writer              = {0};
    cfg_t reader              = {0};
    char url[CFG_URL_MAX]     = {0};
    char ver[CFG_VERSION_MAX] = {0};
    uint32_t count            = 0U;

    TEST_ASSERT_EQUAL_INT(FW_OK, cfg_init(&writer, &store));
    TEST_ASSERT_EQUAL_INT(FW_OK, cfg_manifest_url_set(&writer, "https://h/f.json"));
    TEST_ASSERT_EQUAL_INT(FW_OK, cfg_last_ok_fw_version_set(&writer, "0.1.0"));
    TEST_ASSERT_EQUAL_INT(FW_OK, cfg_check_interval_ms_set(&writer, 60000U));
    TEST_ASSERT_EQUAL_INT(FW_OK, cfg_boot_fail_count_set(&writer, 2U));
    TEST_ASSERT_EQUAL_INT(FW_OK, cfg_save(&writer));
    TEST_ASSERT_EQUAL_INT(FW_OK, cfg_deinit(&writer));

    TEST_ASSERT_EQUAL_INT(FW_OK, cfg_init(&reader, &store));
    TEST_ASSERT_EQUAL_INT(FW_OK, cfg_manifest_url_get(&reader, url, sizeof(url)));
    TEST_ASSERT_EQUAL_INT(FW_OK, cfg_last_ok_fw_version_get(&reader, ver, sizeof(ver)));
    TEST_ASSERT_EQUAL_INT(FW_OK, cfg_boot_fail_count_get(&reader, &count));

    TEST_ASSERT_EQUAL_STRING("https://h/f.json", url);
    TEST_ASSERT_EQUAL_STRING("0.1.0", ver);
    TEST_ASSERT_EQUAL_UINT32(60000U, interval_of(&reader));
    TEST_ASSERT_EQUAL_UINT32(2U, count);
}

/* The one guard that exists because of a constraint a layer above: an
 * interval at or past the update cycle's wrap horizon would read as "already
 * due" on every step. Refused here, where the value arrives, rather than at
 * bring-up minutes later. */
void test_cfg_check_interval_refuses_the_scheduling_horizon(void) {
    fake_store_t fake       = {0};
    const cfg_store_t store = bind_fake(&fake);
    cfg_t c                 = {0};

    TEST_ASSERT_EQUAL_INT(FW_OK, cfg_init(&c, &store));
    TEST_ASSERT_EQUAL_INT(FW_ERR_PARAM, cfg_check_interval_ms_set(&c, CFG_CHECK_INTERVAL_MAX_MS));
    TEST_ASSERT_EQUAL_INT(FW_ERR_PARAM, cfg_check_interval_ms_set(&c, 0xFFFFFFFFU));
    TEST_ASSERT_EQUAL_UINT32(TEST_DEFAULT_CHECK_MS, interval_of(&c));
}

void test_cfg_check_interval_accepts_both_ends_of_its_range(void) {
    fake_store_t fake       = {0};
    const cfg_store_t store = bind_fake(&fake);
    cfg_t c                 = {0};

    TEST_ASSERT_EQUAL_INT(FW_OK, cfg_init(&c, &store));

    /* Zero is a real setting -- checking disabled -- not a missing one. */
    TEST_ASSERT_EQUAL_INT(FW_OK, cfg_check_interval_ms_set(&c, 0U));
    TEST_ASSERT_EQUAL_UINT32(0U, interval_of(&c));

    TEST_ASSERT_EQUAL_INT(FW_OK, cfg_check_interval_ms_set(&c, CFG_CHECK_INTERVAL_MAX_MS - 1U));
    TEST_ASSERT_EQUAL_UINT32(CFG_CHECK_INTERVAL_MAX_MS - 1U, interval_of(&c));
}

void test_cfg_manifest_url_refuses_a_string_that_does_not_fit(void) {
    fake_store_t fake               = {0};
    const cfg_store_t store         = bind_fake(&fake);
    cfg_t c                         = {0};
    char too_long[CFG_URL_MAX + 1U] = {0};
    char just_fits[CFG_URL_MAX]     = {0};
    char read_back[CFG_URL_MAX]     = {0};

    fill(too_long, CFG_URL_MAX);       /* one character past what fits */
    fill(just_fits, CFG_URL_MAX - 1U); /* the longest that does        */

    TEST_ASSERT_EQUAL_INT(FW_OK, cfg_init(&c, &store));
    TEST_ASSERT_EQUAL_INT(FW_OK, cfg_manifest_url_set(&c, "https://keep/me.json"));

    TEST_ASSERT_EQUAL_INT(FW_ERR_PARAM, cfg_manifest_url_set(&c, too_long));
    TEST_ASSERT_EQUAL_INT(FW_ERR_PARAM, cfg_manifest_url_set(&c, ""));

    /* The rejected value must not have half-landed. */
    TEST_ASSERT_EQUAL_INT(FW_OK, cfg_manifest_url_get(&c, read_back, sizeof(read_back)));
    TEST_ASSERT_EQUAL_STRING("https://keep/me.json", read_back);

    TEST_ASSERT_EQUAL_INT(FW_OK, cfg_manifest_url_set(&c, just_fits));
}

void test_cfg_manifest_url_get_refuses_a_buffer_too_small(void) {
    fake_store_t fake       = {0};
    const cfg_store_t store = bind_fake(&fake);
    cfg_t c                 = {0};
    char small[8]           = "XXXXXXX";

    TEST_ASSERT_EQUAL_INT(FW_OK, cfg_init(&c, &store));
    TEST_ASSERT_EQUAL_INT(FW_OK, cfg_manifest_url_set(&c, "1234567"));

    /* Seven characters need eight bytes; seven is one short. */
    TEST_ASSERT_EQUAL_INT(FW_ERR_NO_SPACE, cfg_manifest_url_get(&c, small, 7U));
    TEST_ASSERT_EQUAL_MEMORY("XXXXXXX", small, sizeof(small));

    TEST_ASSERT_EQUAL_INT(FW_OK, cfg_manifest_url_get(&c, small, 8U));
    TEST_ASSERT_EQUAL_STRING("1234567", small);
}

/* R-LFC-05: every operation checks the lifecycle state, so none of them can
 * hand back the contents of an uninitialised struct. */
void test_cfg_every_accessor_is_err_state_before_init(void) {
    cfg_t c               = {0};
    char buf[CFG_URL_MAX] = {0};
    uint32_t value        = 0U;

    TEST_ASSERT_EQUAL_INT(FW_ERR_STATE, cfg_defaults_set(&c));
    TEST_ASSERT_EQUAL_INT(FW_ERR_STATE, cfg_save(&c));
    TEST_ASSERT_EQUAL_INT(FW_ERR_STATE, cfg_manifest_url_get(&c, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_INT(FW_ERR_STATE, cfg_manifest_url_set(&c, "https://x/y.json"));
    TEST_ASSERT_EQUAL_INT(FW_ERR_STATE, cfg_last_ok_fw_version_get(&c, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_INT(FW_ERR_STATE, cfg_last_ok_fw_version_set(&c, "0.1.0"));
    TEST_ASSERT_EQUAL_INT(FW_ERR_STATE, cfg_check_interval_ms_get(&c, &value));
    TEST_ASSERT_EQUAL_INT(FW_ERR_STATE, cfg_check_interval_ms_set(&c, 1000U));
    TEST_ASSERT_EQUAL_INT(FW_ERR_STATE, cfg_boot_fail_count_get(&c, &value));
    TEST_ASSERT_EQUAL_INT(FW_ERR_STATE, cfg_boot_fail_count_set(&c, 1U));
}

/* R-SRC-06: a NULL argument is rejected at the public boundary, before
 * anything changes. An adapter missing half of itself counts as one. */
void test_cfg_rejects_null_arguments(void) {
    fake_store_t fake       = {0};
    const cfg_store_t store = bind_fake(&fake);
    cfg_store_t broken      = store;
    cfg_t c                 = {0};
    char buf[CFG_URL_MAX]   = {0};
    uint32_t value          = 0U;

    TEST_ASSERT_EQUAL_INT(FW_ERR_PARAM, cfg_init(NULL, &store));
    TEST_ASSERT_EQUAL_INT(FW_ERR_PARAM, cfg_init(&c, NULL));

    broken.load = NULL;
    TEST_ASSERT_EQUAL_INT(FW_ERR_PARAM, cfg_init(&c, &broken));
    broken      = store;
    broken.save = NULL;
    TEST_ASSERT_EQUAL_INT(FW_ERR_PARAM, cfg_init(&c, &broken));
    TEST_ASSERT_EQUAL_UINT(0U, fake.load_calls);

    TEST_ASSERT_EQUAL_INT(FW_OK, cfg_init(&c, &store));
    TEST_ASSERT_EQUAL_INT(FW_ERR_PARAM, cfg_defaults_set(NULL));
    TEST_ASSERT_EQUAL_INT(FW_ERR_PARAM, cfg_save(NULL));
    TEST_ASSERT_EQUAL_INT(FW_ERR_PARAM, cfg_manifest_url_get(&c, NULL, sizeof(buf)));
    TEST_ASSERT_EQUAL_INT(FW_ERR_PARAM, cfg_manifest_url_set(&c, NULL));
    TEST_ASSERT_EQUAL_INT(FW_ERR_PARAM, cfg_last_ok_fw_version_get(&c, NULL, sizeof(buf)));
    TEST_ASSERT_EQUAL_INT(FW_ERR_PARAM, cfg_last_ok_fw_version_set(&c, NULL));
    TEST_ASSERT_EQUAL_INT(FW_ERR_PARAM, cfg_check_interval_ms_get(&c, NULL));
    TEST_ASSERT_EQUAL_INT(FW_ERR_PARAM, cfg_boot_fail_count_get(&c, NULL));
    TEST_ASSERT_EQUAL_INT(FW_ERR_PARAM, cfg_check_interval_ms_get(NULL, &value));
}

/* R-LFC-04: repeatable, and safe on an instance that never came up. */
void test_cfg_deinit_is_repeatable_and_safe_half_built(void) {
    fake_store_t fake       = {0};
    const cfg_store_t store = bind_fake(&fake);
    cfg_t c                 = {0};
    uint32_t ms             = 0U;

    TEST_ASSERT_EQUAL_INT(FW_OK, cfg_deinit(&c));
    TEST_ASSERT_EQUAL_INT(FW_ERR_PARAM, cfg_deinit(NULL));

    TEST_ASSERT_EQUAL_INT(FW_OK, cfg_init(&c, &store));
    TEST_ASSERT_EQUAL_INT(FW_OK, cfg_deinit(&c));
    TEST_ASSERT_EQUAL_INT(FW_OK, cfg_deinit(&c));
    TEST_ASSERT_EQUAL_INT(FW_ERR_STATE, cfg_check_interval_ms_get(&c, &ms));

    /* And it comes back up, because deinit left nothing behind. */
    TEST_ASSERT_EQUAL_INT(FW_OK, cfg_init(&c, &store));
}

/* The reason `cfg_save()` is a separate call: a caller that changes four
 * settings pays for one flash write, not four. If a setter ever starts
 * writing, this is what says so. */
void test_cfg_a_setter_never_writes_to_the_store(void) {
    fake_store_t fake       = {0};
    const cfg_store_t store = bind_fake(&fake);
    cfg_t c                 = {0};

    TEST_ASSERT_EQUAL_INT(FW_OK, cfg_init(&c, &store));
    TEST_ASSERT_EQUAL_UINT(0U, fake.save_calls);

    TEST_ASSERT_EQUAL_INT(FW_OK, cfg_manifest_url_set(&c, "https://a/b.json"));
    TEST_ASSERT_EQUAL_INT(FW_OK, cfg_last_ok_fw_version_set(&c, "9.9.9"));
    TEST_ASSERT_EQUAL_INT(FW_OK, cfg_check_interval_ms_set(&c, 1U));
    TEST_ASSERT_EQUAL_INT(FW_OK, cfg_boot_fail_count_set(&c, 7U));
    TEST_ASSERT_EQUAL_INT(FW_OK, cfg_defaults_set(&c));
    TEST_ASSERT_EQUAL_UINT(0U, fake.save_calls);

    TEST_ASSERT_EQUAL_INT(FW_OK, cfg_save(&c));
    TEST_ASSERT_EQUAL_UINT(1U, fake.save_calls);
}

/* -------------------------- Private functions -------------------------- */

static fw_err_t fake_load(void *ctx, void *out, size_t cap, size_t *out_len) {
    fake_store_t *fake = (fake_store_t *)ctx;

    fake->load_calls += 1U;
    if (fake->force_load != FW_OK) {
        return fake->force_load;
    }
    if (!fake->has_blob) {
        return FW_ERR_NOT_FOUND;
    }
    if (cap < fake->len) {
        return FW_ERR_NO_SPACE;
    }

    (void)memcpy(out, &fake->blob, fake->len);
    *out_len = fake->len;
    return FW_OK;
}

static fw_err_t fake_save(void *ctx, const void *data, size_t len) {
    fake_store_t *fake = (fake_store_t *)ctx;

    fake->save_calls += 1U;
    if (len > sizeof(fake->blob)) {
        return FW_ERR_NO_SPACE;
    }

    (void)memcpy(&fake->blob, data, len);
    fake->len      = len;
    fake->has_blob = true;
    return FW_OK;
}

static cfg_store_t bind_fake(fake_store_t *fake) {
    const cfg_store_t store = {
        .load = fake_load,
        .save = fake_save,
        .ctx  = fake,
    };
    return store;
}

static uint32_t interval_of(const cfg_t *c) {
    uint32_t ms = 0U;
    TEST_ASSERT_EQUAL_INT(FW_OK, cfg_check_interval_ms_get(c, &ms));
    return ms;
}

/* Writes `chars` printable characters plus a NUL. The buffer must hold both. */
static void fill(char *out, size_t chars) {
    for (size_t i = 0; i < chars; ++i) {
        out[i] = 'a';
    }
    out[chars] = '\0';
}

/*** end of file ***/
