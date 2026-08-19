#ifndef APPLESCREEN_GLFW_SHIM_H
#define APPLESCREEN_GLFW_SHIM_H

#include <stdbool.h>

// Opaque, matches the forward declaration in the real glfw3.h - we never
// need the full GLFW header, just a pointer we pass through untouched.
typedef struct GLFWwindow GLFWwindow;

typedef GLFWwindow *(*applescreen_glfw_create_window_fn)(int width, int height,
                                                          const char *title,
                                                          void *monitor,
                                                          GLFWwindow *share);
typedef void (*applescreen_glfw_destroy_window_fn)(GLFWwindow *window);
typedef void (*applescreen_glfw_poll_events_fn)(void);

// Called by interpose.c once it has resolved the real function pointer for
// each name, before handing back our wrapper in its place.
void applescreen_glfw_shim_set_real_create_window(void *real_ptr);
void applescreen_glfw_shim_set_real_destroy_window(void *real_ptr);
void applescreen_glfw_shim_set_real_poll_events(void *real_ptr);

// The wrappers interpose.c hands back to the caller instead of the real
// functions. Same signature as the real GLFW functions so they're
// call-compatible drop-ins.
GLFWwindow *applescreen_glfw_shim_create_window(int width, int height,
                                                 const char *title,
                                                 void *monitor,
                                                 GLFWwindow *share);
void applescreen_glfw_shim_destroy_window(GLFWwindow *window);

// Drains the command queue (main-thread-safe: this only ever runs on the
// thread the game calls glfwPollEvents from, which macOS/GLFW already
// requires to be the main thread) before forwarding to the real GLFW
// implementation. This is the one-per-frame safe point the rest of the
// system relies on to execute window-control commands.
void applescreen_glfw_shim_poll_events(void);

// True once glfwCreateWindow has returned a real window and it hasn't been
// destroyed since. Anything that wants to act on the window must check this
// first (acquire semantics) rather than assuming applescreen_glfw_shim_window()
// is valid.
bool applescreen_glfw_shim_window_ready(void);
GLFWwindow *applescreen_glfw_shim_window(void);

#endif /* APPLESCREEN_GLFW_SHIM_H */
