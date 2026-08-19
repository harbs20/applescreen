#include "ipc_server.h"
#include "log.h"

// Runs as soon as dyld finishes loading this dylib into the host process -
// i.e. before the JVM's own main() starts, since DYLD_INSERT_LIBRARIES loads
// us ahead of everything else. The __DATA,__interpose section in interpose.c
// is installed by dyld itself at load time, with no code needed here; this
// constructor only needs to bring up the IPC server so the control app has
// something to connect to as soon as possible.
__attribute__((constructor))
static void applescreen_core_init(void) {
    applescreen_log("=== applescreen core loaded ===");
    applescreen_ipc_server_start();
}
