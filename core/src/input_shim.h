#ifndef APPLESCREEN_INPUT_SHIM_H
#define APPLESCREEN_INPUT_SHIM_H

#include <stdbool.h>

#include "glfw_shim.h"

// Deliberately decoupled from Nuklear: this file only captures raw GLFW
// input events and tracks level/delta state. overlay_gl2.c (which knows
// about Nuklear) reads it once per frame and translates it into nk_input_*
// calls.
typedef struct {
    double cursor_x, cursor_y;
    bool mouse_down[3]; /* left, right, middle */
    double scroll_x, scroll_y;
} applescreen_input_state_t;

// Real GLFW callback signatures (GLFWkeyfun/GLFWcursorposfun/
// GLFWmousebuttonfun/GLFWscrollfun). These matter here in a way they don't
// for glfw_shim.c's hooks: glfwCreateWindow/glfwSwapBuffers are called
// directly by the game every time, so a same-signature wrapper is a
// straight substitute. glfwSetKeyCallback & co. are *setters* - the game
// calls them ONCE, passing its own event handler as an argument - so the
// thing we hand back for dlsym("glfwSetKeyCallback") must itself be a
// same-signature *setter* replacement, not something shaped like the
// per-event handler. (Getting this backwards was tried first and crashed
// the game: LWJGL's call to what it thought was the 2-argument setter
// landed on a 5-argument event-handler-shaped function instead, reading
// garbage for 3 nonexistent arguments and eventually calling through a
// corrupted pointer.)
typedef void (*applescreen_key_callback_fn)(GLFWwindow *, int, int, int, int);
typedef void (*applescreen_cursor_pos_callback_fn)(GLFWwindow *, double, double);
typedef void (*applescreen_mouse_button_callback_fn)(GLFWwindow *, int, int, int);
typedef void (*applescreen_scroll_callback_fn)(GLFWwindow *, double, double);

// Called by interpose.c once it has resolved the real *setter* function
// itself (e.g. the actual glfwSetKeyCallback implementation) - not a
// per-event callback, which isn't known until the game calls one of the
// hooks below and hands it to us as an argument.
void applescreen_input_shim_set_real_set_key_callback(void *real_setter);
void applescreen_input_shim_set_real_set_cursor_pos_callback(void *real_setter);
void applescreen_input_shim_set_real_set_mouse_button_callback(void *real_setter);
void applescreen_input_shim_set_real_set_scroll_callback(void *real_setter);

// The replacement *setter* functions interpose.c hands back in place of
// the real glfwSetXCallback - same signature as those: (window,
// new_callback) -> previous_callback. Each stashes the game's callback and
// tells the real GLFW to invoke this file's own per-event handler instead
// of it directly, so every event is seen here first.
applescreen_key_callback_fn applescreen_input_shim_set_key_callback(GLFWwindow *window,
                                                                     applescreen_key_callback_fn callback);
applescreen_cursor_pos_callback_fn applescreen_input_shim_set_cursor_pos_callback(
    GLFWwindow *window, applescreen_cursor_pos_callback_fn callback);
applescreen_mouse_button_callback_fn applescreen_input_shim_set_mouse_button_callback(
    GLFWwindow *window, applescreen_mouse_button_callback_fn callback);
applescreen_scroll_callback_fn applescreen_input_shim_set_scroll_callback(GLFWwindow *window,
                                                                           applescreen_scroll_callback_fn callback);

// True while the overlay is toggled open (Ctrl+I). While open, mouse
// events are consumed by the UI instead of the game; while closed,
// everything passes through to the game unchanged.
bool applescreen_input_shim_overlay_open(void);

// Main-thread only, called once per frame from overlay_gl2.c. Copies out
// the current state and resets the per-frame scroll delta (cursor position
// and mouse-button-down are level state and are not reset).
void applescreen_input_shim_consume_frame(applescreen_input_state_t *out);

// --- Shared "capture next key" flow, used by both hotkeys.c and (later)
// key_rebind.c's rebind-UI flows, so there is exactly one place that
// decides what happens when a key arrives while some UI element is
// waiting for one. `on_key` is called with the *next* real keydown
// (action==PRESS only) after a capture begins; it returns NULL to accept
// it (capture ends) or a non-NULL error string to reject it and stay in
// capture mode (e.g. a collision with an existing binding) - the error is
// available via applescreen_input_shim_capture_error() for the overlay to
// display. Only one capture can be active at a time; starting a new one
// implicitly cancels any previous one.
typedef const char *(*applescreen_capture_callback_fn)(int key, int mods);

void applescreen_input_shim_begin_capture(applescreen_capture_callback_fn on_key);
bool applescreen_input_shim_capturing(void);
const char *applescreen_input_shim_capture_error(void);

// Cancels any in-progress capture without accepting a binding. Safe to
// call when nothing is capturing (no-op).
void applescreen_input_shim_cancel_capture(void);

// Main-thread only, called once per frame from overlay_gl2.c: advances an
// idle-timeout counter and auto-cancels a capture left open too long
// (counted in frames, not wall-clock time, to avoid needing a glfwGetTime
// resolve just for this).
void applescreen_input_shim_tick_capture_timeout(void);

// Small shared human-readable formatter ("Ctrl+1", "Shift+F5", ...) used
// by both the Hotkeys and Rebinds overlay tabs. Writes into the caller's
// own `out` buffer rather than returning a pointer to a shared static one
// - deliberately, after a real bug where two calls in the same
// snprintf()/applescreen_log() expression silently clobbered each other's
// result through a shared buffer (only the second call's text ever got
// read, for *both* substitutions).
void applescreen_input_shim_format_key_label(int key, int mods, char *out, int out_size);

#endif /* APPLESCREEN_INPUT_SHIM_H */
