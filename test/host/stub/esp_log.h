/**
 * @file    esp_log.h
 * @author  dtbao
 * @date    2026-09-06
 * @brief   Host fake for the ESP-IDF logging header (R-TST-07).
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

#ifndef HOST_STUB_ESP_LOG_H
#define HOST_STUB_ESP_LOG_H

/* ------------------------------ Includes ------------------------------- */

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------- Constants & macros ------------------------- */

/* The macros swallow the line rather than printing it: a test suite that
 * prints every log line hides its own failures. The call is still compiled,
 * so an argument with a side effect still runs and an unused variable is
 * still used. */
#define ESP_LOGE(tag, ...) host_log_sink((tag), __VA_ARGS__)
#define ESP_LOGW(tag, ...) host_log_sink((tag), __VA_ARGS__)
#define ESP_LOGI(tag, ...) host_log_sink((tag), __VA_ARGS__)
#define ESP_LOGD(tag, ...) host_log_sink((tag), __VA_ARGS__)
#define ESP_LOGV(tag, ...) host_log_sink((tag), __VA_ARGS__)

/* ------------------------ Public function prototypes ------------------- */

/**
 * @brief   Discards a log line, after the compiler has checked it.
 * @param   tag   module tag, ignored
 * @param   fmt   printf format; checked against the arguments at compile time
 * @note    The printf attribute is the point of this fake: a `%lu` against an
 *          `int` is a corrupted field log on the target, and this is where it
 *          gets caught instead.
 */
#ifdef __GNUC__
#define HOST_LOG_PRINTF_CHECK __attribute__((format(printf, 2, 3)))
#else
#define HOST_LOG_PRINTF_CHECK
#endif

static inline void host_log_sink(const char *tag, const char *fmt, ...)
    HOST_LOG_PRINTF_CHECK;

static inline void host_log_sink(const char *tag, const char *fmt, ...)
{
    (void) tag;
    (void) fmt;
}

#ifdef __cplusplus
}
#endif

#endif /* HOST_STUB_ESP_LOG_H */

/*** end of file ***/
