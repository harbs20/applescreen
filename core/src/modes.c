#include "modes.h"

#include <string.h>

#include "log.h"
#include "window_control.h"

static const applescreen_mode_t k_modes[] = {
    {"Default", false, 854, 480, false, 0.0f},
    {"Fullscreen", true, 0, 0, false, 0.0f},
    {"Thin", false, 420, 720, false, 0.0f},
    {"Wide", false, 1600, 400, false, 0.0f},
    {"Tall", false, 500, 1000, false, 0.0f},
};
#define APPLESCREEN_MODE_COUNT (int)(sizeof(k_modes) / sizeof(k_modes[0]))

static const applescreen_mode_t *g_active_mode = NULL;

int applescreen_modes_count(void) {
    return APPLESCREEN_MODE_COUNT;
}

const applescreen_mode_t *applescreen_modes_get(int index) {
    if (index < 0 || index >= APPLESCREEN_MODE_COUNT) return NULL;
    return &k_modes[index];
}

const applescreen_mode_t *applescreen_modes_find(const char *id) {
    if (!id) return NULL;
    for (int i = 0; i < APPLESCREEN_MODE_COUNT; i++) {
        if (strcmp(k_modes[i].id, id) == 0) return &k_modes[i];
    }
    return NULL;
}

void applescreen_modes_switch(const char *id) {
    const applescreen_mode_t *mode = applescreen_modes_find(id);
    if (!mode) {
        applescreen_log("modes: unknown mode \"%s\"", id ? id : "(null)");
        return;
    }

    int monitor_w = 0, monitor_h = 0;
    applescreen_window_control_get_monitor_size(&monitor_w, &monitor_h);

    int target_w = mode->fullscreen ? monitor_w : mode->width;
    int target_h = mode->fullscreen ? monitor_h : mode->height;
    if (target_w <= 0 || target_h <= 0) {
        applescreen_log("modes: switch to \"%s\" aborted, no usable size (monitor query failed?)", mode->id);
        return;
    }

    int pos_x = mode->fullscreen ? 0 : (monitor_w > target_w ? (monitor_w - target_w) / 2 : 0);
    int pos_y = mode->fullscreen ? 0 : (monitor_h > target_h ? (monitor_h - target_h) / 2 : 0);

    applescreen_window_control_set_size(target_w, target_h);
    applescreen_window_control_set_pos(pos_x, pos_y);

    g_active_mode = mode;
    applescreen_log("modes: switched to \"%s\" (%dx%d at %d,%d)", mode->id, target_w, target_h, pos_x, pos_y);
}

const char *applescreen_modes_active_id(void) {
    return g_active_mode ? g_active_mode->id : NULL;
}

bool applescreen_modes_active_sensitivity_override(float *out) {
    if (!g_active_mode || !g_active_mode->sensitivity_override_enabled) return false;
    if (out) *out = g_active_mode->sensitivity_override;
    return true;
}
