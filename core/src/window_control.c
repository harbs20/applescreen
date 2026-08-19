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

// Lazily resolved and cached on first use. Only ever touched from the main
// thread (window_control_dispatch runs exclusively there), so no locking.
static set_window_pos_fn g_set_window_pos = NULL;
static set_window_size_fn g_set_window_size = NULL;
static focus_window_fn g_focus_window = NULL;
static get_window_pos_fn g_get_window_pos = NULL;
static get_window_size_fn g_get_window_size = NULL;
static set_window_should_close_fn g_set_window_should_close = NULL;

// Resolves *slot on demand via the captured glfw handle if not already
// cached. This is the fallback path that works even for GLFW entry points
// LWJGL never happened to resolve via dlsym itself.
static void *resolve_cached(void **slot, const char *name) {
    if (!*slot) {
        *slot = applescreen_resolve_glfw_symbol(name);
    }
    return *slot;
}

static void fail(applescreen_response_t *response, applescreen_status_t status) {
    response->magic = APPLESCREEN_IPC_MAGIC;
    response->status = status;
    response->value1 = 0;
    response->value2 = 0;
}

static void ok(applescreen_response_t *response, int32_t value1, int32_t value2) {
    response->magic = APPLESCREEN_IPC_MAGIC;
    response->status = APPLESCREEN_STATUS_OK;
    response->value1 = value1;
    response->value2 = value2;
}

void applescreen_window_control_dispatch(const applescreen_command_t *command,
                                          applescreen_response_t *response) {
    response->request_id = command->request_id;

    if (command->magic != APPLESCREEN_IPC_MAGIC || command->version != APPLESCREEN_IPC_VERSION) {
        applescreen_log("window_control: bad magic/version on request_id=%u", command->request_id);
        fail(response, APPLESCREEN_STATUS_BAD_VERSION);
        return;
    }

    if (command->type == APPLESCREEN_CMD_PING) {
        ok(response, 0, 0);
        return;
    }

    if (!applescreen_glfw_shim_window_ready()) {
        applescreen_log("window_control: request_id=%u type=%d with no window yet",
                         command->request_id, command->type);
        fail(response, APPLESCREEN_STATUS_NO_WINDOW);
        return;
    }

    GLFWwindow *window = applescreen_glfw_shim_window();

    switch (command->type) {
        case APPLESCREEN_CMD_SET_WINDOW_POS: {
            set_window_pos_fn fn = (set_window_pos_fn)resolve_cached((void **)&g_set_window_pos, "glfwSetWindowPos");
            if (!fn) { fail(response, APPLESCREEN_STATUS_NO_WINDOW); return; }
            fn(window, command->arg1, command->arg2);
            ok(response, command->arg1, command->arg2);
            return;
        }
        case APPLESCREEN_CMD_SET_WINDOW_SIZE: {
            set_window_size_fn fn = (set_window_size_fn)resolve_cached((void **)&g_set_window_size, "glfwSetWindowSize");
            if (!fn) { fail(response, APPLESCREEN_STATUS_NO_WINDOW); return; }
            fn(window, command->arg1, command->arg2);
            ok(response, command->arg1, command->arg2);
            return;
        }
        case APPLESCREEN_CMD_FOCUS_WINDOW: {
            focus_window_fn fn = (focus_window_fn)resolve_cached((void **)&g_focus_window, "glfwFocusWindow");
            if (!fn) { fail(response, APPLESCREEN_STATUS_NO_WINDOW); return; }
            fn(window);
            ok(response, 0, 0);
            return;
        }
        case APPLESCREEN_CMD_GET_WINDOW_POS: {
            get_window_pos_fn fn = (get_window_pos_fn)resolve_cached((void **)&g_get_window_pos, "glfwGetWindowPos");
            if (!fn) { fail(response, APPLESCREEN_STATUS_NO_WINDOW); return; }
            int x = 0, y = 0;
            fn(window, &x, &y);
            ok(response, x, y);
            return;
        }
        case APPLESCREEN_CMD_GET_WINDOW_SIZE: {
            get_window_size_fn fn = (get_window_size_fn)resolve_cached((void **)&g_get_window_size, "glfwGetWindowSize");
            if (!fn) { fail(response, APPLESCREEN_STATUS_NO_WINDOW); return; }
            int w = 0, h = 0;
            fn(window, &w, &h);
            ok(response, w, h);
            return;
        }
        case APPLESCREEN_CMD_SET_WINDOW_SHOULD_CLOSE: {
            set_window_should_close_fn fn =
                (set_window_should_close_fn)resolve_cached((void **)&g_set_window_should_close, "glfwSetWindowShouldClose");
            if (!fn) { fail(response, APPLESCREEN_STATUS_NO_WINDOW); return; }
            fn(window, command->arg1);
            ok(response, command->arg1, 0);
            return;
        }
        default:
            applescreen_log("window_control: unknown command type=%d", command->type);
            fail(response, APPLESCREEN_STATUS_UNKNOWN_COMMAND);
            return;
    }
}
