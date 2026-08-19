#ifndef APPLESCREEN_WINDOW_CONTROL_H
#define APPLESCREEN_WINDOW_CONTROL_H

#include "applescreen/ipc_protocol.h"

// Main-thread only - executes one command against the real GLFW window and
// fills in *response. Called exclusively from command_queue's drain loop.
void applescreen_window_control_dispatch(const applescreen_command_t *command,
                                          applescreen_response_t *response);

#endif /* APPLESCREEN_WINDOW_CONTROL_H */
