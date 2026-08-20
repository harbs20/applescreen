#ifndef APPLESCREEN_GLFW_SHIM_H
#define APPLESCREEN_GLFW_SHIM_H

#include <stdbool.h>

// Opaque, matches the forward declarations in the real glfw3.h - we never
// need the full GLFW header, just pointers we pass through untouched.
typedef struct GLFWwindow GLFWwindow;
typedef struct GLFWmonitor GLFWmonitor;

typedef GLFWwindow *(*applescreen_glfw_create_window_fn)(int width, int height,
                                                          const char *title,
                                                          void *monitor,
                                                          GLFWwindow *share);
typedef void (*applescreen_glfw_destroy_window_fn)(GLFWwindow *window);
typedef void (*applescreen_glfw_swap_buffers_fn)(GLFWwindow *window);

// Called by interpose.c once it has resolved the real function pointer for
// each name, before handing back our wrapper in its place.
void applescreen_glfw_shim_set_real_create_window(void *real_ptr);
void applescreen_glfw_shim_set_real_destroy_window(void *real_ptr);
void applescreen_glfw_shim_set_real_swap_buffers(void *real_ptr);

// The wrappers interpose.c hands back to the caller instead of the real
// functions. Same signature as the real GLFW functions so they're
// call-compatible drop-ins.
GLFWwindow *applescreen_glfw_shim_create_window(int width, int height,
                                                 const char *title,
                                                 void *monitor,
                                                 GLFWwindow *share);
void applescreen_glfw_shim_destroy_window(GLFWwindow *window);

// Renders the overlay (applescreen_overlay_render_frame(), see overlay_gl2.h)
// on top of whatever the game just drew, then forwards to the real
// glfwSwapBuffers. This only ever runs on the thread the game calls
// glfwSwapBuffers from, which macOS/GLFW already requires to be the main
// thread - the same guarantee window_control.c's direct, synchronous GLFW
// calls from the overlay's UI callbacks rely on.
void applescreen_glfw_shim_swap_buffers(GLFWwindow *window);

// True once glfwCreateWindow has returned a real window and it hasn't been
// destroyed since. Anything that wants to act on the window must check this
// first (acquire semantics) rather than assuming applescreen_glfw_shim_window()
// is valid.
bool applescreen_glfw_shim_window_ready(void);
GLFWwindow *applescreen_glfw_shim_window(void);

#endif /* APPLESCREEN_GLFW_SHIM_H */
