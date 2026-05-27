#include <stddef.h>
#include <stdarg.h>

#include "logging.h"

static log_sink_fn s_global_sink = NULL;
static log_level_t s_min_level = LOG_OFF;
static void* s_user_ctx = NULL;

void log_set_global_sink(log_sink_fn sink, log_level_t min_level, void* user_ctx)
{
    if (sink)
    {
        s_global_sink = sink;
        s_min_level = min_level;
        s_user_ctx = user_ctx;
    }
    else
    {
        s_global_sink = NULL;
        s_min_level = LOG_OFF;
        s_user_ctx = NULL;
    }
}

void internal_log_write(log_level_t level, const char* file, int line, const char* msg, ...) {
    va_list args;
    va_start(args, msg);
    if (level >= s_min_level) {
        if (s_global_sink) {
            s_global_sink(level, file, line, msg, args, s_user_ctx);
        }
    }
    va_end(args);
}
