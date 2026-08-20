#ifndef APPLESCREEN_CONFIG_H
#define APPLESCREEN_CONFIG_H

// Flat text persistence: one "category.name.field=value" pair per line, at
// ~/Library/Application Support/Applescreen/config.txt. Hand-rolled parser
// (split on the first '=', then the key on '.') rather than a real
// format/library - the value set here is small enough that a dependency
// isn't worth it.
//
// Modules that own persisted state (hotkeys.c, key_rebind.c, sensitivity.c)
// each register themselves so load/save can walk them generically, rather
// than config.c knowing the shape of every module's data.

// Called once by a module during its own init, before applescreen_config_load()
// runs (see init.c's constructor ordering) or applescreen_config_save() is
// ever called.
typedef void (*applescreen_config_apply_fn)(const char *key, const char *value);
typedef void (*applescreen_config_write_fn)(void (*emit)(const char *key, const char *value));

// key_prefix (e.g. "hotkey") is matched against the parsed key's first
// dot-separated segment; the full remaining key (e.g. "toggle_overlay.key")
// is passed to apply() unchanged so each module parses its own sub-shape.
void applescreen_config_register(const char *key_prefix, applescreen_config_apply_fn apply,
                                  applescreen_config_write_fn write);

// Reads the config file if it exists (silently does nothing if it
// doesn't - first run), dispatching each line to whichever registered
// module's prefix matches. Call once, after all modules have registered.
void applescreen_config_load(void);

// Rewrites the whole file from every registered module's current
// in-memory state. Called after any hotkey rebind, key-rebind edit, or
// sensitivity change - not on mode switches, which are transient.
void applescreen_config_save(void);

#endif /* APPLESCREEN_CONFIG_H */
