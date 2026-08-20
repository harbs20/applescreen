#include "glfw_shim.h"

#include <stdatomic.h>

#include "log.h"
#include "overlay_gl2.h"

static applescreen_glfw_create_window_fn g_real_create_window = NULL;
static applescreen_glfw_destroy_window_fn g_real_destroy_window = NULL;
static applescreen_glfw_swap_buffers_fn g_real_swap_buffers = NULL;

static GLFWwindow *g_window = NULL;
static atomic_bool g_window_ready = false;

void applescreen_glfw_shim_set_real_create_window(void *real_ptr) {
    g_real_create_window = (applescreen_glfw_create_window_fn)real_ptr;
}

void applescreen_glfw_shim_set_real_destroy_window(void *real_ptr) {
    g_real_destroy_window = (applescreen_glfw_destroy_window_fn)real_ptr;
}

void applescreen_glfw_shim_set_real_swap_buffers(void *real_ptr) {
    g_real_swap_buffers = (applescreen_glfw_swap_buffers_fn)real_ptr;
}

GLFWwindow *applescreen_glfw_shim_create_window(int width, int height, const char *title,
                                                 void *monitor, GLFWwindow *share) {
    // Runs on whatever thread the game calls glfwCreateWindow from - GLFW
    // itself requires that to be the main thread on macOS, so we inherit
    // that guarantee rather than needing our own synchronization here.
    GLFWwindow *window = g_real_create_window ? g_real_create_window(width, height, title, monitor, share)
                                               : NULL;
    if (window) {
        g_window = window;
        atomic_store_explicit(&g_window_ready, true, memory_order_release);
        applescreen_log("glfw_shim: window created %p (%dx%d)", window, width, height);
    } else {
        applescreen_log("glfw_shim: real glfwCreateWindow returned NULL or was never resolved");
    }
    return window;
}

void applescreen_glfw_shim_destroy_window(GLFWwindow *window) {
    if (window == g_window) {
        atomic_store_explicit(&g_window_ready, false, memory_order_release);
        g_window = NULL;
        applescreen_log("glfw_shim: window %p destroyed", window);
    }
    if (g_real_destroy_window) {
        g_real_destroy_window(window);
    }
}

void applescreen_glfw_shim_swap_buffers(GLFWwindow *window) {
    if (window == g_window) {
        applescreen_overlay_render_frame(window);
    }
    if (g_real_swap_buffers) {
        g_real_swap_buffers(window);
    }
}

bool applescreen_glfw_shim_window_ready(void) {
    return atomic_load_explicit(&g_window_ready, memory_order_acquire);
}

GLFWwindow *applescreen_glfw_shim_window(void) {
    return g_window;
}
