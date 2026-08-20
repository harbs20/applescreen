#ifndef APPLESCREEN_WINDOW_CONTROL_H
#define APPLESCREEN_WINDOW_CONTROL_H

#include <stdbool.h>

// Main-thread only - each of these resolves the needed real GLFW pointer on
// demand (cached after first use) and calls straight through to the real
// window. Safe to call directly and synchronously from the overlay's
// render-thread UI callbacks (glfwSwapBuffers, like glfwPollEvents before
// it, is guaranteed to run on the same main thread GLFW requires for every
// window/context call on macOS) - no queue/IPC needed.
//
// Each is a no-op if there's no window yet (applescreen_glfw_shim_window_ready()
// is false) or if the underlying GLFW symbol was never resolvable.
void applescreen_window_control_set_pos(int x, int y);
void applescreen_window_control_set_size(int width, int height);
void applescreen_window_control_focus(void);
void applescreen_window_control_get_pos(int *x, int *y);
void applescreen_window_control_get_size(int *width, int *height);
void applescreen_window_control_set_should_close(bool value);

// Real screen resolution of the primary monitor, via
// glfwGetPrimaryMonitor()+glfwGetVideoMode() - used by modes.c for
// "Fullscreen" (fills the actual screen rather than a fixed guess).
// *width/*height are set to 0 if unavailable (no monitor resolved yet).
void applescreen_window_control_get_monitor_size(int *width, int *height);

#endif /* APPLESCREEN_WINDOW_CONTROL_H */
