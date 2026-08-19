#include "log.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <time.h>

static const char *kLogPath = "/tmp/applescreen_core.log";
static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;

void applescreen_log(const char *fmt, ...) {
    pthread_mutex_lock(&g_log_mutex);

    FILE *f = fopen(kLogPath, "a");
    if (f) {
        time_t now = time(NULL);
        struct tm tm_info;
        localtime_r(&now, &tm_info);
        char ts[32];
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm_info);
        fprintf(f, "[%s] ", ts);

        va_list args;
        va_start(args, fmt);
        vfprintf(f, fmt, args);
        va_end(args);

        fprintf(f, "\n");
        fclose(f);
    }

    pthread_mutex_unlock(&g_log_mutex);
}
