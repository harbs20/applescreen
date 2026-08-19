#include "interpose.h"

#include <string.h>

#include "glfw_shim.h"
#include "log.h"

static void *hooked_dlsym(void *handle, const char *symbol);

typedef struct {
    void *replacement;
    void *replacee;
} applescreen_interpose_t;

// Mach-O linker feature: when this dylib is loaded via DYLD_INSERT_LIBRARIES,
// dyld rewrites every reference to `dlsym` in the process (including calls
// made from within this dylib itself) to `hooked_dlsym` instead. This is the
// whole mechanism the rest of the injection depends on.
//
// `replacee` doubles as our only safe way to get back to the *real*
// implementation: it's populated by a plain data relocation to `dlsym`'s
// address, resolved before dyld applies any interpose rewriting (dyld reads
// this exact value to know what to intercept, so it cannot itself already
// be rewritten). Do not "helpfully" bootstrap a real_dlsym pointer by
// calling dlsym(RTLD_NEXT, "dlsym") instead - that call is itself a
// reference to the symbol `dlsym` and gets rewritten to hooked_dlsym just
// like every other call site in the process, recursing forever. Verified
// empirically: an earlier version of this file did exactly that and
// stack-overflowed within milliseconds of injection.
//
// Not `const`: the modern linker's read-only-data migration pass moves
// const-qualified `__DATA,__interpose` content into `__DATA_CONST`, and
// dyld's interpose scanner does not reliably find it there on this
// toolchain (verified: otool showed segname __DATA_CONST instead of __DATA
// until this was made non-const and linked with -ld_classic). Every
// real-world interpose example uses plain __DATA - don't gamble on
// __DATA_CONST also being honored.
__attribute__((used)) __attribute__((section("__DATA,__interpose")))
static applescreen_interpose_t g_interpose_dlsym = {
    (void *)hooked_dlsym,
    (void *)dlsym,
};

void *applescreen_real_dlsym(void *handle, const char *symbol) {
    void *(*real)(void *, const char *) = (void *(*)(void *, const char *))g_interpose_dlsym.replacee;
    return real(handle, symbol);
}

static void *g_glfw_handle = NULL;

void *applescreen_glfw_handle(void) {
    return g_glfw_handle;
}

void *applescreen_resolve_glfw_symbol(const char *name) {
    if (!g_glfw_handle) {
        return NULL;
    }
    void *ptr = applescreen_real_dlsym(g_glfw_handle, name);
    applescreen_log("interpose: on-demand resolve \"%s\" -> %p", name, ptr);
    return ptr;
}

static int is_glfw_symbol(const char *symbol) {
    return symbol && strncmp(symbol, "glfw", 4) == 0;
}

// The master hook. Every symbol name the process resolves via dlsym flows
// through here - normally passed straight through to the real dlsym, except
// for the handful of GLFW entry points glfw_shim.c needs to override.
//
// Deliberately does NOT log unconditionally: dlsym is called an enormous
// number of times during process startup by unrelated system frameworks
// (observed ~130k calls for a single unrelated symbol in a few seconds
// during M0 spike testing) - unconditional per-call file I/O here would be
// a severe startup slowdown against a real JVM. Only "glfw*" lookups are
// logged.
static void *hooked_dlsym(void *handle, const char *symbol) {
    void *real = applescreen_real_dlsym(handle, symbol);

    if (is_glfw_symbol(symbol)) {
        if (!g_glfw_handle) {
            g_glfw_handle = handle;
            applescreen_log("interpose: captured glfw handle %p via \"%s\"", handle, symbol);
        }
        applescreen_log("interpose: observed dlsym(\"%s\") -> %p", symbol, real);

        if (strcmp(symbol, "glfwCreateWindow") == 0) {
            applescreen_glfw_shim_set_real_create_window(real);
            return (void *)applescreen_glfw_shim_create_window;
        }
        if (strcmp(symbol, "glfwDestroyWindow") == 0) {
            applescreen_glfw_shim_set_real_destroy_window(real);
            return (void *)applescreen_glfw_shim_destroy_window;
        }
        if (strcmp(symbol, "glfwPollEvents") == 0) {
            applescreen_glfw_shim_set_real_poll_events(real);
            return (void *)applescreen_glfw_shim_poll_events;
        }
    }

    return real;
}
