// Thin umbrella so Swift can `import CIPCProtocol` and get the exact same
// struct layout the injected core (core/include/applescreen/ipc_protocol.h)
// uses on the wire - single canonical definition, no hand-mirrored Swift
// structs to keep in sync.
#include "../../../../core/include/applescreen/ipc_protocol.h"
