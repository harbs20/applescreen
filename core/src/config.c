#include "config.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "log.h"

#define APPLESCREEN_CONFIG_MAX_MODULES 8
#define APPLESCREEN_CONFIG_MAX_LINE 256

typedef struct {
    char prefix[32];
    applescreen_config_apply_fn apply;
    applescreen_config_write_fn write;
} registered_module_t;

static registered_module_t g_modules[APPLESCREEN_CONFIG_MAX_MODULES];
static int g_module_count = 0;

void applescreen_config_register(const char *key_prefix, applescreen_config_apply_fn apply,
                                  applescreen_config_write_fn write) {
    if (g_module_count >= APPLESCREEN_CONFIG_MAX_MODULES) {
        applescreen_log("config: too many registered modules, dropping \"%s\"", key_prefix);
        return;
    }
    registered_module_t *m = &g_modules[g_module_count++];
    strncpy(m->prefix, key_prefix, sizeof(m->prefix) - 1);
    m->prefix[sizeof(m->prefix) - 1] = '\0';
    m->apply = apply;
    m->write = write;
}

// Builds ~/Library/Application Support/Applescreen/config.txt into `out`,
// creating the directory tree if needed. Returns false if $HOME is unset.
static bool config_path(char *out, size_t out_size) {
    const char *home = getenv("HOME");
    if (!home || !*home) return false;

    char dir[APPLESCREEN_CONFIG_MAX_LINE];
    snprintf(dir, sizeof(dir), "%s/Library/Application Support/Applescreen", home);

    // mkdir -p equivalent: create each path component, ignoring EEXIST.
    char partial[APPLESCREEN_CONFIG_MAX_LINE];
    partial[0] = '\0';
    const char *segment = dir;
    while (*segment) {
        const char *slash = strchr(segment + 1, '/');
        size_t len = slash ? (size_t)(slash - dir) : strlen(dir);
        strncpy(partial, dir, len);
        partial[len] = '\0';
        mkdir(partial, 0755); /* ignore errors (EEXIST is expected/fine) */
        if (!slash) break;
        segment = slash;
    }

    snprintf(out, out_size, "%s/config.txt", dir);
    return true;
}

static registered_module_t *find_module(const char *prefix) {
    for (int i = 0; i < g_module_count; i++) {
        if (strcmp(g_modules[i].prefix, prefix) == 0) return &g_modules[i];
    }
    return NULL;
}

void applescreen_config_load(void) {
    char path[APPLESCREEN_CONFIG_MAX_LINE];
    if (!config_path(path, sizeof(path))) return;

    FILE *f = fopen(path, "r");
    if (!f) {
        applescreen_log("config: no existing config at %s (first run)", path);
        return;
    }

    char line[APPLESCREEN_CONFIG_MAX_LINE];
    int applied = 0;
    while (fgets(line, sizeof(line), f)) {
        char *newline = strchr(line, '\n');
        if (newline) *newline = '\0';
        if (line[0] == '\0' || line[0] == '#') continue;

        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char *full_key = line;
        const char *value = eq + 1;

        char *dot = strchr(full_key, '.');
        if (!dot) continue;
        *dot = '\0';
        const char *prefix = full_key;
        const char *rest = dot + 1;

        registered_module_t *m = find_module(prefix);
        if (m && m->apply) {
            m->apply(rest, value);
            applied++;
        }
    }
    fclose(f);
    applescreen_log("config: loaded %d entries from %s", applied, path);
}

static FILE *g_save_file = NULL;
static const char *g_save_prefix = NULL;

static void emit_line(const char *key, const char *value) {
    if (!g_save_file) return;
    fprintf(g_save_file, "%s.%s=%s\n", g_save_prefix, key, value);
}

void applescreen_config_save(void) {
    char path[APPLESCREEN_CONFIG_MAX_LINE];
    if (!config_path(path, sizeof(path))) return;

    FILE *f = fopen(path, "w");
    if (!f) {
        applescreen_log("config: failed to open %s for writing", path);
        return;
    }

    g_save_file = f;
    for (int i = 0; i < g_module_count; i++) {
        if (!g_modules[i].write) continue;
        g_save_prefix = g_modules[i].prefix;
        g_modules[i].write(emit_line);
    }
    g_save_file = NULL;
    g_save_prefix = NULL;

    fclose(f);
    applescreen_log("config: saved to %s", path);
}
