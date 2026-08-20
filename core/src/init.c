#include "config.h"
#include "log.h"

// Runs as soon as dyld finishes loading this dylib into the host process -
// i.e. before the JVM's own main() starts, since DYLD_INSERT_LIBRARIES loads
// us ahead of everything else. The __DATA,__interpose section in interpose.c
// is installed by dyld itself at load time - nothing else needs to happen
// here. The overlay/input hooks are purely reactive (installed the moment
// LWJGL resolves the relevant GLFW symbols via dlsym), so there's no
// separate subsystem to start up like v1's IPC server needed.
//
// applescreen_config_load() runs here, once. Modules with persisted state
// (hotkeys.c/key_rebind.c/sensitivity.c, from v3 onward) register
// themselves with config.c from their own lower-numbered constructors
// (priority < 200) so they've registered before this fires - constructor
// priority is ascending order, lower runs first.
__attribute__((constructor(200)))
static void applescreen_core_init(void) {
    applescreen_log("=== applescreen core loaded ===");
    applescreen_config_load();
}
