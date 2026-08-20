#include "window_control.h"

#include <stddef.h>

#include "glfw_shim.h"
#include "interpose.h"
#include "log.h"

typedef void (*set_window_pos_fn)(GLFWwindow *, int, int);
typedef void (*set_window_size_fn)(GLFWwindow *, int, int);
typedef void (*focus_window_fn)(GLFWwindow *);
typedef void (*get_window_pos_fn)(GLFWwindow *, int *, int *);
typedef void (*get_window_size_fn)(GLFWwindow *, int *, int *);
typedef void (*set_window_should_close_fn)(GLFWwindow *, int);

// Matches the real glfw3.h GLFWvidmode struct field order exactly - stable
// GLFW ABI, same convention as the hardcoded GLFW key/mod constants
// elsewhere in this project (never linked, only relied on as a fixed data
// layout).
typedef struct {
    int width;
    int height;
    int redBits;
    int greenBits;
    int blueBits;
    int refreshRate;
} applescreen_glfw_vidmode_t;

typedef GLFWmonitor *(*get_primary_monitor_fn)(void);
typedef const applescreen_glfw_vidmode_t *(*get_video_mode_fn)(GLFWmonitor *);

// Lazily resolved and cached on first use. Only ever touched from the main
// thread, so no locking.
static set_window_pos_fn g_set_window_pos = NULL;
static set_window_size_fn g_set_window_size = NULL;
static focus_window_fn g_focus_window = NULL;
static get_window_pos_fn g_get_window_pos = NULL;
static get_window_size_fn g_get_window_size = NULL;
static set_window_should_close_fn g_set_window_should_close = NULL;
static get_primary_monitor_fn g_get_primary_monitor = NULL;
static get_video_mode_fn g_get_video_mode = NULL;

// Resolves *slot on demand via the captured glfw handle if not already
// cached. This is the fallback path that works even for GLFW entry points
// LWJGL never happened to resolve via dlsym itself.
static void *resolve_cached(void **slot, const char *name) {
    if (!*slot) {
        *slot = applescreen_resolve_glfw_symbol(name);
    }
    return *slot;
}

void applescreen_window_control_set_pos(int x, int y) {
    if (!applescreen_glfw_shim_window_ready()) return;
    set_window_pos_fn fn = (set_window_pos_fn)resolve_cached((void **)&g_set_window_pos, "glfwSetWindowPos");
    if (!fn) return;
    fn(applescreen_glfw_shim_window(), x, y);
}

void applescreen_window_control_set_size(int width, int height) {
    if (!applescreen_glfw_shim_window_ready()) return;
    set_window_size_fn fn = (set_window_size_fn)resolve_cached((void **)&g_set_window_size, "glfwSetWindowSize");
    if (!fn) return;
    fn(applescreen_glfw_shim_window(), width, height);
}

void applescreen_window_control_focus(void) {
    if (!applescreen_glfw_shim_window_ready()) return;
    focus_window_fn fn = (focus_window_fn)resolve_cached((void **)&g_focus_window, "glfwFocusWindow");
    if (!fn) return;
    fn(applescreen_glfw_shim_window());
}

void applescreen_window_control_get_pos(int *x, int *y) {
    if (x) *x = 0;
    if (y) *y = 0;
    if (!applescreen_glfw_shim_window_ready()) return;
    get_window_pos_fn fn = (get_window_pos_fn)resolve_cached((void **)&g_get_window_pos, "glfwGetWindowPos");
    if (!fn) return;
    fn(applescreen_glfw_shim_window(), x, y);
}

void applescreen_window_control_get_size(int *width, int *height) {
    if (width) *width = 0;
    if (height) *height = 0;
    if (!applescreen_glfw_shim_window_ready()) return;
    get_window_size_fn fn = (get_window_size_fn)resolve_cached((void **)&g_get_window_size, "glfwGetWindowSize");
    if (!fn) return;
    fn(applescreen_glfw_shim_window(), width, height);
}

void applescreen_window_control_set_should_close(bool value) {
    if (!applescreen_glfw_shim_window_ready()) return;
    set_window_should_close_fn fn =
        (set_window_should_close_fn)resolve_cached((void **)&g_set_window_should_close, "glfwSetWindowShouldClose");
    if (!fn) return;
    fn(applescreen_glfw_shim_window(), value ? 1 : 0);
}

void applescreen_window_control_get_monitor_size(int *width, int *height) {
    if (width) *width = 0;
    if (height) *height = 0;

    get_primary_monitor_fn get_monitor =
        (get_primary_monitor_fn)resolve_cached((void **)&g_get_primary_monitor, "glfwGetPrimaryMonitor");
    get_video_mode_fn get_mode = (get_video_mode_fn)resolve_cached((void **)&g_get_video_mode, "glfwGetVideoMode");
    if (!get_monitor || !get_mode) return;

    GLFWmonitor *monitor = get_monitor();
    if (!monitor) return;

    const applescreen_glfw_vidmode_t *mode = get_mode(monitor);
    if (!mode) return;

    if (width) *width = mode->width;
    if (height) *height = mode->height;
}
