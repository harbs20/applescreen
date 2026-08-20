#include "input_shim.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "hotkeys.h"
#include "key_rebind.h"
#include "log.h"

// GLFW's real glfw3.h defines letter keys as their ASCII codes and these
// specific constants (stable across GLFW versions - not resolved
// dynamically, this project never links glfw3.h at all, see interpose.c).
#define APPLESCREEN_GLFW_KEY_I 73
#define APPLESCREEN_GLFW_MOD_SHIFT 0x0001
#define APPLESCREEN_GLFW_MOD_CONTROL 0x0002
#define APPLESCREEN_GLFW_MOD_ALT 0x0004
#define APPLESCREEN_GLFW_MOD_SUPER 0x0008
#define APPLESCREEN_GLFW_PRESS 1
#define APPLESCREEN_GLFW_MOUSE_BUTTON_LEFT 0
#define APPLESCREEN_GLFW_MOUSE_BUTTON_RIGHT 1
#define APPLESCREEN_GLFW_MOUSE_BUTTON_MIDDLE 2

// ~5 seconds at a typical 60fps - counted in frames (ticked from
// overlay_gl2.c's per-frame render call), not wall-clock time, to avoid
// needing a glfwGetTime resolve just for this.
#define APPLESCREEN_CAPTURE_TIMEOUT_FRAMES 300

// The real *setter* functions (glfwSetKeyCallback itself, etc.) - resolved
// once via dlsym, called from inside our own setter replacements below to
// register OUR event handlers as what GLFW actually invokes.
typedef applescreen_key_callback_fn (*set_key_callback_setter_fn)(GLFWwindow *, applescreen_key_callback_fn);
typedef applescreen_cursor_pos_callback_fn (*set_cursor_pos_callback_setter_fn)(GLFWwindow *,
                                                                                 applescreen_cursor_pos_callback_fn);
typedef applescreen_mouse_button_callback_fn (*set_mouse_button_callback_setter_fn)(
    GLFWwindow *, applescreen_mouse_button_callback_fn);
typedef applescreen_scroll_callback_fn (*set_scroll_callback_setter_fn)(GLFWwindow *,
                                                                         applescreen_scroll_callback_fn);

static set_key_callback_setter_fn g_real_set_key_callback = NULL;
static set_cursor_pos_callback_setter_fn g_real_set_cursor_pos_callback = NULL;
static set_mouse_button_callback_setter_fn g_real_set_mouse_button_callback = NULL;
static set_scroll_callback_setter_fn g_real_set_scroll_callback = NULL;

// The GAME's own event handlers - not known until it actually calls one of
// our setter replacements and hands us its callback as an argument, so
// these can't be populated from interpose.c the way glfw_shim.c's
// real-pointer setters are.
static applescreen_key_callback_fn g_game_key_callback = NULL;
static applescreen_cursor_pos_callback_fn g_game_cursor_pos_callback = NULL;
static applescreen_mouse_button_callback_fn g_game_mouse_button_callback = NULL;
static applescreen_scroll_callback_fn g_game_scroll_callback = NULL;

static atomic_bool g_overlay_open = false;

// Only ever touched from the main thread (GLFW dispatches these callbacks
// there, and overlay_gl2.c's frame consumer runs there too), so no locking.
static applescreen_input_state_t g_state = {0};

// Shared capture-next-key state (see input_shim.h). Only one capture can
// be active; only ever touched from the main thread.
static applescreen_capture_callback_fn g_capture_callback = NULL;
static const char *g_capture_error = NULL;
static int g_capture_idle_frames = 0;

void applescreen_input_shim_begin_capture(applescreen_capture_callback_fn on_key) {
    g_capture_callback = on_key;
    g_capture_error = NULL;
    g_capture_idle_frames = 0;
}

bool applescreen_input_shim_capturing(void) {
    return g_capture_callback != NULL;
}

const char *applescreen_input_shim_capture_error(void) {
    return g_capture_error;
}

void applescreen_input_shim_cancel_capture(void) {
    g_capture_callback = NULL;
    g_capture_error = NULL;
    g_capture_idle_frames = 0;
}

void applescreen_input_shim_tick_capture_timeout(void) {
    if (!g_capture_callback) return;
    g_capture_idle_frames++;
    if (g_capture_idle_frames >= APPLESCREEN_CAPTURE_TIMEOUT_FRAMES) {
        applescreen_log("input_shim: capture timed out, cancelling");
        applescreen_input_shim_cancel_capture();
    }
}

void applescreen_input_shim_format_key_label(int key, int mods, char *out, int out_size) {
    char prefix[24] = "";
    if (mods & APPLESCREEN_GLFW_MOD_CONTROL) strncat(prefix, "Ctrl+", sizeof(prefix) - strlen(prefix) - 1);
    if (mods & APPLESCREEN_GLFW_MOD_SHIFT) strncat(prefix, "Shift+", sizeof(prefix) - strlen(prefix) - 1);
    if (mods & APPLESCREEN_GLFW_MOD_ALT) strncat(prefix, "Alt+", sizeof(prefix) - strlen(prefix) - 1);
    if (mods & APPLESCREEN_GLFW_MOD_SUPER) strncat(prefix, "Cmd+", sizeof(prefix) - strlen(prefix) - 1);

    const char *name;
    char name_buf[16];
    if ((key >= '0' && key <= '9') || (key >= 'A' && key <= 'Z')) {
        name_buf[0] = (char)key;
        name_buf[1] = '\0';
        name = name_buf;
    } else if (key == 32) {
        name = "Space";
    } else if (key == 256) {
        name = "Escape";
    } else if (key == 257) {
        name = "Enter";
    } else if (key == 258) {
        name = "Tab";
    } else if (key >= 290 && key <= 301) {
        snprintf(name_buf, sizeof(name_buf), "F%d", key - 289);
        name = name_buf;
    } else {
        snprintf(name_buf, sizeof(name_buf), "Key%d", key);
        name = name_buf;
    }

    snprintf(out, (size_t)out_size, "%s%s", prefix, name);
}

void applescreen_input_shim_set_real_set_key_callback(void *real_setter) {
    g_real_set_key_callback = (set_key_callback_setter_fn)real_setter;
}

void applescreen_input_shim_set_real_set_cursor_pos_callback(void *real_setter) {
    g_real_set_cursor_pos_callback = (set_cursor_pos_callback_setter_fn)real_setter;
}

void applescreen_input_shim_set_real_set_mouse_button_callback(void *real_setter) {
    g_real_set_mouse_button_callback = (set_mouse_button_callback_setter_fn)real_setter;
}

void applescreen_input_shim_set_real_set_scroll_callback(void *real_setter) {
    g_real_set_scroll_callback = (set_scroll_callback_setter_fn)real_setter;
}

bool applescreen_input_shim_overlay_open(void) {
    return atomic_load_explicit(&g_overlay_open, memory_order_acquire);
}

// The actual per-event handlers - these are what GLFW ends up calling
// directly on real input events, once our setter replacements below have
// registered them in place of the game's own handlers.

static void key_event_handler(GLFWwindow *window, int key, int scancode, int action, int mods) {
    // Ctrl+I is checked first, unconditionally, always - and as a side
    // effect force-clears any in-progress capture (uncommitted) rather
    // than being swallowed *by* that capture, so it always works as an
    // escape hatch regardless of what UI state is open.
    if (key == APPLESCREEN_GLFW_KEY_I && (mods & APPLESCREEN_GLFW_MOD_CONTROL) &&
        action == APPLESCREEN_GLFW_PRESS) {
        if (applescreen_input_shim_capturing()) {
            applescreen_input_shim_cancel_capture();
        }
        bool was_open = atomic_load_explicit(&g_overlay_open, memory_order_acquire);
        atomic_store_explicit(&g_overlay_open, !was_open, memory_order_release);
        applescreen_log("input_shim: overlay %s", was_open ? "closed" : "opened");
        return; /* always swallow the toggle itself, regardless of overlay state */
    }

    // Capture-next-key flow (Hotkeys/Rebinds tab "rebind" buttons): while
    // active, every keydown is offered to the capture callback and
    // swallowed, whether accepted or rejected as a collision. A callback
    // is allowed to chain into a *new* capture before returning (e.g.
    // key_rebind.c's "capture From, then capture To" add flow) by calling
    // applescreen_input_shim_begin_capture() itself - detect that by
    // comparing against the callback we actually invoked, so we don't
    // clobber a capture the callback just started.
    if (g_capture_callback && action == APPLESCREEN_GLFW_PRESS) {
        applescreen_capture_callback_fn active = g_capture_callback;
        const char *error = active(key, mods);
        g_capture_error = error;
        if (!error) {
            if (g_capture_callback == active) {
                g_capture_callback = NULL;
            }
            /* else: callback already chained into a new capture - leave it */
        } else {
            g_capture_idle_frames = 0; /* activity happened, even if rejected */
        }
        return;
    }

    if (action == APPLESCREEN_GLFW_PRESS && applescreen_hotkeys_dispatch(key, mods)) {
        return; /* a bound action (e.g. mode switch) handled it - swallow */
    }

    int remapped_key = applescreen_key_rebind_apply(key, mods);

    if (g_game_key_callback) {
        g_game_key_callback(window, remapped_key, scancode, action, mods);
    }
}

static void cursor_pos_event_handler(GLFWwindow *window, double xpos, double ypos) {
    g_state.cursor_x = xpos;
    g_state.cursor_y = ypos;

    if (!applescreen_input_shim_overlay_open() && g_game_cursor_pos_callback) {
        g_game_cursor_pos_callback(window, xpos, ypos);
    }
}

static void mouse_button_event_handler(GLFWwindow *window, int button, int action, int mods) {
    bool down = (action == APPLESCREEN_GLFW_PRESS);
    if (button == APPLESCREEN_GLFW_MOUSE_BUTTON_LEFT) {
        g_state.mouse_down[0] = down;
    } else if (button == APPLESCREEN_GLFW_MOUSE_BUTTON_RIGHT) {
        g_state.mouse_down[1] = down;
    } else if (button == APPLESCREEN_GLFW_MOUSE_BUTTON_MIDDLE) {
        g_state.mouse_down[2] = down;
    }

    if (!applescreen_input_shim_overlay_open() && g_game_mouse_button_callback) {
        g_game_mouse_button_callback(window, button, action, mods);
    }
}

static void scroll_event_handler(GLFWwindow *window, double xoffset, double yoffset) {
    g_state.scroll_x += xoffset;
    g_state.scroll_y += yoffset;

    if (!applescreen_input_shim_overlay_open() && g_game_scroll_callback) {
        g_game_scroll_callback(window, xoffset, yoffset);
    }
}

// The setter replacements - same signature as the real glfwSetXCallback
// functions they stand in for: stash the game's callback, tell the real
// GLFW to call our event handler instead, return the previous callback
// (matching real setter semantics, for whatever the game does with it).

applescreen_key_callback_fn applescreen_input_shim_set_key_callback(GLFWwindow *window,
                                                                     applescreen_key_callback_fn callback) {
    applescreen_key_callback_fn previous = g_game_key_callback;
    g_game_key_callback = callback;
    if (g_real_set_key_callback) {
        g_real_set_key_callback(window, key_event_handler);
    }
    return previous;
}

applescreen_cursor_pos_callback_fn applescreen_input_shim_set_cursor_pos_callback(
    GLFWwindow *window, applescreen_cursor_pos_callback_fn callback) {
    applescreen_cursor_pos_callback_fn previous = g_game_cursor_pos_callback;
    g_game_cursor_pos_callback = callback;
    if (g_real_set_cursor_pos_callback) {
        g_real_set_cursor_pos_callback(window, cursor_pos_event_handler);
    }
    return previous;
}

applescreen_mouse_button_callback_fn applescreen_input_shim_set_mouse_button_callback(
    GLFWwindow *window, applescreen_mouse_button_callback_fn callback) {
    applescreen_mouse_button_callback_fn previous = g_game_mouse_button_callback;
    g_game_mouse_button_callback = callback;
    if (g_real_set_mouse_button_callback) {
        g_real_set_mouse_button_callback(window, mouse_button_event_handler);
    }
    return previous;
}

applescreen_scroll_callback_fn applescreen_input_shim_set_scroll_callback(GLFWwindow *window,
                                                                           applescreen_scroll_callback_fn callback) {
    applescreen_scroll_callback_fn previous = g_game_scroll_callback;
    g_game_scroll_callback = callback;
    if (g_real_set_scroll_callback) {
        g_real_set_scroll_callback(window, scroll_event_handler);
    }
    return previous;
}

void applescreen_input_shim_consume_frame(applescreen_input_state_t *out) {
    *out = g_state;
    g_state.scroll_x = 0;
    g_state.scroll_y = 0;
}
