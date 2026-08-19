// M0 spike: proves DYLD_INSERT_LIBRARIES injection reaches a real Minecraft/JVM
// process on macOS, and that dlsym interposition observes GLFW symbol lookups.
// See docs/RISKS.md and the plan for what this is de-risking. No overrides -
// every call passes through unchanged, we only observe.
//
// Usage: after building and ad-hoc signing, set
//   DYLD_INSERT_LIBRARIES=/absolute/path/to/libapplescreen_spike.dylib
// as a per-instance environment variable in Prism Launcher/MultiMC, launch
// Minecraft normally, then inspect /tmp/applescreen_spike.log - specifically
// `grep -i glfw /tmp/applescreen_spike.log`. dlsym is called an enormous
// number of times by unrelated system frameworks during process startup
// (observed ~130k calls for one unrelated symbol alone during local testing
// on this machine), so the raw log can grow large fast; only every Nth
// non-glfw call is sampled to keep it inspectable, while every glfw* call is
// always logged in full.

#include <dlfcn.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static const char *kLogPath = "/tmp/applescreen_spike.log";
static FILE *g_log_file = NULL;

static void log_line(const char *fmt, ...) {
    if (!g_log_file) return;
    time_t now = time(NULL);
    struct tm tm_info;
    localtime_r(&now, &tm_info);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm_info);
    fprintf(g_log_file, "[%s] ", ts);
    va_list args;
    va_start(args, fmt);
    vfprintf(g_log_file, fmt, args);
    va_end(args);
    fprintf(g_log_file, "\n");
}

static int is_glfw_symbol(const char *symbol) {
    return symbol && strncmp(symbol, "glfw", 4) == 0;
}

typedef struct interpose_s {
    void *replacement;
    void *replacee;
} interpose_t;

static void *hooked_dlsym(void *handle, const char *symbol);

// See the long comment in core/src/interpose.c for why this must be
// non-const, plain __DATA (linked with -ld_classic), and why `replacee` -
// not a bootstrapped dlsym(RTLD_NEXT, "dlsym") call - is the only safe way
// to reach the real implementation from inside an interposed dlsym.
__attribute__((used)) __attribute__((section("__DATA,__interpose")))
static interpose_t interpose_dlsym = {
    (void *)hooked_dlsym,
    (void *)dlsym,
};

static void *hooked_dlsym(void *handle, const char *symbol) {
    void *(*real)(void *, const char *) = (void *(*)(void *, const char *))interpose_dlsym.replacee;
    void *result = real(handle, symbol);

    if (is_glfw_symbol(symbol)) {
        log_line("dlsym(%p, \"%s\") -> %p  [GLFW]", handle, symbol, result);
    } else {
        // Sample non-glfw traffic so the log stays readable instead of
        // filling up with tens of thousands of unrelated lookups.
        static _Atomic int counter = 0;
        if (atomic_fetch_add(&counter, 1) % 500 == 0) {
            log_line("dlsym(%p, \"%s\") -> %p  [sampled]", handle, symbol ? symbol : "(null)", result);
        }
    }

    return result;
}

__attribute__((constructor)) static void spike_init(void) {
    g_log_file = fopen(kLogPath, "a");
    if (g_log_file) {
        setvbuf(g_log_file, NULL, _IOLBF, 0);
    }
    log_line("=== applescreen spike loaded ===");
}
