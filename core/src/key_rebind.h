#ifndef APPLESCREEN_KEY_REBIND_H
#define APPLESCREEN_KEY_REBIND_H

#include <stdbool.h>

// Key-to-key remapping: when the game would have received `from_key` (with
// exactly `mods` held), it receives `to_key` instead - the physically-held
// modifiers are still forwarded unchanged, only the key identity changes.
// Checked in input_shim.c's key handler after hotkey dispatch, before
// forwarding to the game.
int applescreen_key_rebind_count(void);
bool applescreen_key_rebind_get(int index, int *from_key, int *to_key, int *mods);
void applescreen_key_rebind_remove(int index);

// Main-thread only. If key+mods matches an entry, returns the remapped
// key; otherwise returns `key` unchanged. Never changes mods.
int applescreen_key_rebind_apply(int key, int mods);

// Starts a two-step capture (via input_shim.c's shared mechanism): first
// captures the "from" key, then chains directly into capturing the "to"
// key, then commits a new entry. Rejects a "from" key already used by a
// different entry (collision) - stays in capture with an inline error.
void applescreen_key_rebind_begin_add(void);

#endif /* APPLESCREEN_KEY_REBIND_H */
