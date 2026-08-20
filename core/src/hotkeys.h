#ifndef APPLESCREEN_HOTKEYS_H
#define APPLESCREEN_HOTKEYS_H

#include <stdbool.h>

// One hotkey entry per built-in mode (see modes.h) - switching modes is
// the only bindable action for this pass. Ctrl+I (overlay toggle) is
// intentionally NOT part of this table: it's reserved, handled directly
// in input_shim.c, and never reassignable.
int applescreen_hotkeys_count(void);
const char *applescreen_hotkeys_mode_id(int index);
bool applescreen_hotkeys_get_binding(int index, int *key, int *mods); /* false if unbound */

// Main-thread only. If key+mods matches a bound entry, switches to that
// entry's mode and returns true (caller should swallow the event);
// otherwise returns false and does nothing.
bool applescreen_hotkeys_dispatch(int key, int mods);

// Starts a capture-next-key flow (via input_shim.c's shared mechanism) to
// rebind entry `index`. Rejects Ctrl+I as a candidate (reserved) and any
// key already bound to a *different* entry (collision) - both cases stay
// in capture with an inline error rather than silently overwriting.
void applescreen_hotkeys_begin_rebind(int index);

#endif /* APPLESCREEN_HOTKEYS_H */
