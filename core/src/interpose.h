#ifndef APPLESCREEN_INTERPOSE_H
#define APPLESCREEN_INTERPOSE_H

#include <dlfcn.h>

// Raw libc dlsym, resolved once via dlsym(RTLD_NEXT, "dlsym"). Everything in
// this codebase that needs to resolve a symbol at runtime (including
// glfw_shim's on-demand fallback lookups) must go through this pointer, never
// through the public `dlsym` symbol - that one is interposed by us, and
// calling it recurses forever.
void *applescreen_real_dlsym(void *handle, const char *symbol);

// The dlopen handle most recently observed resolving a "glfw*" symbol. Once
// non-NULL, it can be used with applescreen_real_dlsym() to resolve *any*
// GLFW entry point on demand, even one the game itself never dlsym'd - this
// is the fallback path that makes window_control work regardless of exactly
// which symbols LWJGL happens to resolve via dlsym vs. static linking.
void *applescreen_glfw_handle(void);

// Resolves `name` against the captured GLFW handle via applescreen_real_dlsym.
// Returns NULL if no glfw handle has been observed yet, or the symbol isn't
// found. Callers (window_control.c) are expected to cache the result
// themselves rather than re-resolving on every command.
void *applescreen_resolve_glfw_symbol(const char *name);

#endif /* APPLESCREEN_INTERPOSE_H */
