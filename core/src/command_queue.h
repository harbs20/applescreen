#ifndef APPLESCREEN_COMMAND_QUEUE_H
#define APPLESCREEN_COMMAND_QUEUE_H

#include <pthread.h>
#include <stdbool.h>

#include "applescreen/ipc_protocol.h"

// One request in flight between a socket connection-handler thread and the
// main thread's drain loop. Stack-allocated by the connection thread, alive
// only for the duration of a single submit/wait/read-response cycle.
typedef struct {
    applescreen_command_t command;
    applescreen_response_t response;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool done;
} applescreen_pending_request_t;

void applescreen_pending_request_init(applescreen_pending_request_t *req,
                                       const applescreen_command_t *command);
void applescreen_pending_request_destroy(applescreen_pending_request_t *req);

// Enqueues `req` and blocks the calling thread until the main thread has
// filled in req->response and signaled completion. Returns false without
// blocking if the queue is full (defined, non-crashing overflow behavior —
// should never actually happen at v1's command rate); the caller is
// responsible for synthesizing an error response in that case.
bool applescreen_command_queue_submit(applescreen_pending_request_t *req);

// Main-thread only. Called once per frame from inside the wrapped
// glfwPollEvents. Drains and dispatches every currently-queued request,
// waking each submitter as it finishes. Never blocks.
void applescreen_command_queue_drain(void);

#endif /* APPLESCREEN_COMMAND_QUEUE_H */
