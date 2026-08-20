#ifndef APPLESCREEN_MODES_H
#define APPLESCREEN_MODES_H

#include <stdbool.h>

// Named window-size presets, modeled on tuxinjector's ModeConfig/mode_system
// (ids literally taken from its own shipped defaults) - a stripped-down
// version for this pass: geometry + an optional per-mode sensitivity
// override, not the transitions/overlays/mirrors tuxinjector's full
// ModeConfig also carries. Sizes are fixed in code, not persisted (only
// hotkey *bindings* to modes are customizable/persisted, via hotkeys.c).
typedef struct {
    const char *id;
    bool fullscreen; /* true: fill the real monitor size at switch time, ignoring width/height */
    int width;
    int height;
    bool sensitivity_override_enabled;
    float sensitivity_override;
} applescreen_mode_t;

int applescreen_modes_count(void);
const applescreen_mode_t *applescreen_modes_get(int index);
const applescreen_mode_t *applescreen_modes_find(const char *id);

// Main-thread only (calls straight into window_control.c). Resizes and
// centers the window on the primary monitor for the named mode; no-op if
// the id doesn't match a known mode or there's no window yet.
void applescreen_modes_switch(const char *id);

// The id of the most recently switched-to mode, or NULL if none yet this
// session (switching is transient, not persisted/restored on launch).
const char *applescreen_modes_active_id(void);

// If the active mode has a sensitivity override, fills *out and returns
// true; otherwise returns false and leaves *out untouched. sensitivity.c
// checks this to decide base-vs-override.
bool applescreen_modes_active_sensitivity_override(float *out);

#endif /* APPLESCREEN_MODES_H */
