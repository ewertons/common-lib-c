#ifndef COMMON_LIB_C_LOG_H
#define COMMON_LIB_C_LOG_H

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum log_level_tag
{
    LOG_TRACE = 0,
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
    LOG_OFF
} log_level_t;

typedef void (*log_sink_fn)(
    log_level_t level,
    const char* file,
    int line,
    const char* msg,
    va_list args,
    void* user_ctx
);

void log_set_global_sink(log_sink_fn sink, log_level_t min_level, void* user_ctx);

void internal_log_write(log_level_t level, const char* file, int line, const char* msg, ...);

#define log_write(level, msg, ...) \
    do { \
        internal_log_write(level, __FILE__, __LINE__, msg, ##__VA_ARGS__); \
    } while (0)

#ifndef LOG_LEVEL_TRACE_DISABLED
#define log_trace(msg, ...) log_write(LOG_TRACE, msg, ##__VA_ARGS__)
#else
#define log_trace(msg, ...) (void)0
#endif

#ifndef LOG_LEVEL_DEBUG_DISABLED
#define log_debug(msg, ...) log_write(LOG_DEBUG, msg, ##__VA_ARGS__)
#else
#define log_debug(msg, ...) (void)0
#endif

#ifndef LOG_LEVEL_INFO_DISABLED
#define log_info(msg, ...) log_write(LOG_INFO, msg, ##__VA_ARGS__)
#else
#define log_info(msg, ...) (void)0
#endif

#ifndef LOG_LEVEL_WARN_DISABLED
#define log_warn(msg, ...) log_write(LOG_WARN, msg, ##__VA_ARGS__)
#else
#define log_warn(msg, ...) (void)0
#endif

#ifndef LOG_LEVEL_ERROR_DISABLED
#define log_error(msg, ...) log_write(LOG_ERROR, msg, ##__VA_ARGS__)
#else
#define log_error(msg, ...) (void)0
#endif

#ifdef __cplusplus
}
#endif

#endif /* COMMON_LIB_C_LOG_H */
