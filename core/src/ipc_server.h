#ifndef APPLESCREEN_IPC_SERVER_H
#define APPLESCREEN_IPC_SERVER_H

// Starts the AF_UNIX/SOCK_SEQPACKET server on a detached background thread.
// Safe to call once from the dylib's constructor (init.c). Never blocks the
// calling thread.
void applescreen_ipc_server_start(void);

#endif /* APPLESCREEN_IPC_SERVER_H */
