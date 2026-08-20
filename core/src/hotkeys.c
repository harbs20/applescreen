#include "hotkeys.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "input_shim.h"
#include "log.h"
#include "modes.h"

// Same stable GLFW constants convention as input_shim.c/modes.c - never
// linked, just fixed data this project relies on.
#define APPLESCREEN_GLFW_KEY_I 73
#define APPLESCREEN_GLFW_KEY_0 48
#define APPLESCREEN_GLFW_MOD_CONTROL 0x0002

#define APPLESCREEN_HOTKEYS_MAX 8

typedef struct {
    bool has_binding;
    int key;
    int mods;
} hotkey_entry_t;

static hotkey_entry_t g_entries[APPLESCREEN_HOTKEYS_MAX];
static int g_entry_count = 0;
static int g_rebinding_index = -1;

static void ensure_initialized(void) {
    if (g_entry_count > 0) return;
    g_entry_count = applescreen_modes_count();
    if (g_entry_count > APPLESCREEN_HOTKEYS_MAX) g_entry_count = APPLESCREEN_HOTKEYS_MAX;
    for (int i = 0; i < g_entry_count; i++) {
        // Default bindings: Ctrl+0 through Ctrl+(count-1), matching modes.c's
        // fixed array order (Default/Fullscreen/Thin/Wide/Tall) - only
        // meaningful while count <= 10 (digit keys); anything past that
        // ships unbound rather than silently reusing a digit.
        if (i < 10) {
            g_entries[i].has_binding = true;
            g_entries[i].key = APPLESCREEN_GLFW_KEY_0 + i;
            g_entries[i].mods = APPLESCREEN_GLFW_MOD_CONTROL;
        } else {
            g_entries[i].has_binding = false;
        }
    }
}

int applescreen_hotkeys_count(void) {
    ensure_initialized();
    return g_entry_count;
}

const char *applescreen_hotkeys_mode_id(int index) {
    const applescreen_mode_t *mode = applescreen_modes_get(index);
    return mode ? mode->id : NULL;
}

bool applescreen_hotkeys_get_binding(int index, int *key, int *mods) {
    ensure_initialized();
    if (index < 0 || index >= g_entry_count || !g_entries[index].has_binding) return false;
    if (key) *key = g_entries[index].key;
    if (mods) *mods = g_entries[index].mods;
    return true;
}

bool applescreen_hotkeys_dispatch(int key, int mods) {
    ensure_initialized();
    for (int i = 0; i < g_entry_count; i++) {
        if (g_entries[i].has_binding && g_entries[i].key == key && g_entries[i].mods == mods) {
            applescreen_modes_switch(applescreen_hotkeys_mode_id(i));
            return true;
        }
    }
    return false;
}

static const char *rebind_capture_callback(int key, int mods) {
    if (key == APPLESCREEN_GLFW_KEY_I && (mods & APPLESCREEN_GLFW_MOD_CONTROL)) {
        return "Ctrl+I is reserved for the overlay toggle";
    }

    for (int i = 0; i < g_entry_count; i++) {
        if (i == g_rebinding_index) continue;
        if (g_entries[i].has_binding && g_entries[i].key == key && g_entries[i].mods == mods) {
            static char buf[64];
            snprintf(buf, sizeof(buf), "Already bound to %s", applescreen_hotkeys_mode_id(i));
            return buf;
        }
    }

    g_entries[g_rebinding_index].has_binding = true;
    g_entries[g_rebinding_index].key = key;
    g_entries[g_rebinding_index].mods = mods;
    char label[48];
    applescreen_input_shim_format_key_label(key, mods, label, sizeof(label));
    applescreen_log("hotkeys: bound %s to %s", applescreen_hotkeys_mode_id(g_rebinding_index), label);
    g_rebinding_index = -1;
    applescreen_config_save();
    return NULL;
}

void applescreen_hotkeys_begin_rebind(int index) {
    ensure_initialized();
    if (index < 0 || index >= g_entry_count) return;
    g_rebinding_index = index;
    applescreen_input_shim_begin_capture(rebind_capture_callback);
}

static void config_apply(const char *key, const char *value) {
    ensure_initialized();
    char key_copy[64];
    strncpy(key_copy, key, sizeof(key_copy) - 1);
    key_copy[sizeof(key_copy) - 1] = '\0';

    char *dot = strrchr(key_copy, '.');
    if (!dot) return;
    *dot = '\0';
    const char *mode_id = key_copy;
    const char *field = dot + 1;

    for (int i = 0; i < g_entry_count; i++) {
        if (strcmp(applescreen_hotkeys_mode_id(i), mode_id) != 0) continue;
        if (strcmp(field, "key") == 0) {
            g_entries[i].key = atoi(value);
            g_entries[i].has_binding = true;
        } else if (strcmp(field, "mods") == 0) {
            g_entries[i].mods = atoi(value);
        }
        return;
    }
}

static void config_write(void (*emit)(const char *, const char *)) {
    ensure_initialized();
    char key_buf[64];
    char value_buf[16];
    for (int i = 0; i < g_entry_count; i++) {
        if (!g_entries[i].has_binding) continue;
        snprintf(key_buf, sizeof(key_buf), "%s.key", applescreen_hotkeys_mode_id(i));
        snprintf(value_buf, sizeof(value_buf), "%d", g_entries[i].key);
        emit(key_buf, value_buf);
        snprintf(key_buf, sizeof(key_buf), "%s.mods", applescreen_hotkeys_mode_id(i));
        snprintf(value_buf, sizeof(value_buf), "%d", g_entries[i].mods);
        emit(key_buf, value_buf);
    }
}

__attribute__((constructor(150))) static void hotkeys_register_config(void) {
    ensure_initialized();
    applescreen_config_register("hotkey", config_apply, config_write);
}
