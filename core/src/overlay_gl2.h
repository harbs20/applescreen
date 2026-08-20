#ifndef APPLESCREEN_OVERLAY_GL2_H
#define APPLESCREEN_OVERLAY_GL2_H

#include "glfw_shim.h"

// Called from the glfwSwapBuffers hook, once per frame, only for the real
// game window. Lazily initializes Nuklear + its font atlas on first call.
// Always processes accumulated input (so Nuklear's internal frame state
// stays consistent even while the panel is closed), but only builds and
// draws the UI when applescreen_input_shim_overlay_open() is true.
void applescreen_overlay_render_frame(GLFWwindow *window);

#endif /* APPLESCREEN_OVERLAY_GL2_H */
