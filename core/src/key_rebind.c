#include "key_rebind.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "input_shim.h"
#include "log.h"

#define APPLESCREEN_KEY_REBIND_MAX 16

typedef struct {
    bool used;
    int from_key;
    int to_key;
    int mods;
} rebind_entry_t;

static rebind_entry_t g_entries[APPLESCREEN_KEY_REBIND_MAX];

static int find_by_from(int key, int mods, int exclude_index) {
    for (int i = 0; i < APPLESCREEN_KEY_REBIND_MAX; i++) {
        if (i == exclude_index || !g_entries[i].used) continue;
        if (g_entries[i].from_key == key && g_entries[i].mods == mods) return i;
    }
    return -1;
}

static int find_free_slot(void) {
    for (int i = 0; i < APPLESCREEN_KEY_REBIND_MAX; i++) {
        if (!g_entries[i].used) return i;
    }
    return -1;
}

int applescreen_key_rebind_count(void) {
    int n = 0;
    for (int i = 0; i < APPLESCREEN_KEY_REBIND_MAX; i++) {
        if (g_entries[i].used) n++;
    }
    return n;
}

// Public API is a dense 0..count-1 view over the sparse slot array, since
// slots can free up out of order after a remove().
static int nth_used_slot(int nth) {
    int n = 0;
    for (int i = 0; i < APPLESCREEN_KEY_REBIND_MAX; i++) {
        if (!g_entries[i].used) continue;
        if (n == nth) return i;
        n++;
    }
    return -1;
}

bool applescreen_key_rebind_get(int index, int *from_key, int *to_key, int *mods) {
    int slot = nth_used_slot(index);
    if (slot < 0) return false;
    if (from_key) *from_key = g_entries[slot].from_key;
    if (to_key) *to_key = g_entries[slot].to_key;
    if (mods) *mods = g_entries[slot].mods;
    return true;
}

void applescreen_key_rebind_remove(int index) {
    int slot = nth_used_slot(index);
    if (slot < 0) return;
    g_entries[slot].used = false;
    applescreen_config_save();
}

int applescreen_key_rebind_apply(int key, int mods) {
    int slot = find_by_from(key, mods, -1);
    return slot >= 0 ? g_entries[slot].to_key : key;
}

static int g_pending_slot = -1;
static int g_pending_from_key = 0;
static int g_pending_from_mods = 0;

static const char *capture_to_callback(int key, int mods) {
    g_entries[g_pending_slot].used = true;
    g_entries[g_pending_slot].from_key = g_pending_from_key;
    g_entries[g_pending_slot].to_key = key;
    g_entries[g_pending_slot].mods = g_pending_from_mods;
    (void)mods; /* the "to" key's own mods aren't part of what we store - only its identity */
    char from_label[48], to_label[48];
    applescreen_input_shim_format_key_label(g_pending_from_key, g_pending_from_mods, from_label, sizeof(from_label));
    applescreen_input_shim_format_key_label(key, 0, to_label, sizeof(to_label));
    applescreen_log("key_rebind: added %s -> %s", from_label, to_label);
    g_pending_slot = -1;
    applescreen_config_save();
    return NULL;
}

static const char *capture_from_callback(int key, int mods) {
    if (find_by_from(key, mods, -1) >= 0) {
        return "That key is already rebound";
    }
    int slot = find_free_slot();
    if (slot < 0) {
        return "Rebind list is full";
    }
    g_pending_slot = slot;
    g_pending_from_key = key;
    g_pending_from_mods = mods;
    // Chain directly into capturing the "to" key - input_shim.c's key
    // handler detects this re-entrant begin_capture() call and won't
    // clobber it (see the comment there).
    applescreen_input_shim_begin_capture(capture_to_callback);
    return NULL;
}

void applescreen_key_rebind_begin_add(void) {
    applescreen_input_shim_begin_capture(capture_from_callback);
}

static void config_apply(const char *key, const char *value) {
    // Keys look like "<slot>.from"/"<slot>.to"/"<slot>.mods".
    char key_copy[64];
    strncpy(key_copy, key, sizeof(key_copy) - 1);
    key_copy[sizeof(key_copy) - 1] = '\0';

    char *dot = strrchr(key_copy, '.');
    if (!dot) return;
    *dot = '\0';
    int slot = atoi(key_copy);
    const char *field = dot + 1;
    if (slot < 0 || slot >= APPLESCREEN_KEY_REBIND_MAX) return;

    if (strcmp(field, "from") == 0) {
        g_entries[slot].from_key = atoi(value);
        g_entries[slot].used = true;
    } else if (strcmp(field, "to") == 0) {
        g_entries[slot].to_key = atoi(value);
    } else if (strcmp(field, "mods") == 0) {
        g_entries[slot].mods = atoi(value);
    }
}

static void config_write(void (*emit)(const char *, const char *)) {
    char key_buf[32];
    char value_buf[16];
    for (int i = 0; i < APPLESCREEN_KEY_REBIND_MAX; i++) {
        if (!g_entries[i].used) continue;
        snprintf(key_buf, sizeof(key_buf), "%d.from", i);
        snprintf(value_buf, sizeof(value_buf), "%d", g_entries[i].from_key);
        emit(key_buf, value_buf);
        snprintf(key_buf, sizeof(key_buf), "%d.to", i);
        snprintf(value_buf, sizeof(value_buf), "%d", g_entries[i].to_key);
        emit(key_buf, value_buf);
        snprintf(key_buf, sizeof(key_buf), "%d.mods", i);
        snprintf(value_buf, sizeof(value_buf), "%d", g_entries[i].mods);
        emit(key_buf, value_buf);
    }
}

__attribute__((constructor(150))) static void key_rebind_register_config(void) {
    applescreen_config_register("keyrebind", config_apply, config_write);
}
