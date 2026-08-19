#include "command_queue.h"

#include <string.h>

#include "log.h"
#include "window_control.h"

#define APPLESCREEN_QUEUE_CAPACITY 64

static applescreen_pending_request_t *g_ring[APPLESCREEN_QUEUE_CAPACITY];
static size_t g_head = 0;
static size_t g_tail = 0;
static size_t g_count = 0;
static pthread_mutex_t g_ring_mutex = PTHREAD_MUTEX_INITIALIZER;

void applescreen_pending_request_init(applescreen_pending_request_t *req,
                                       const applescreen_command_t *command) {
    req->command = *command;
    memset(&req->response, 0, sizeof(req->response));
    pthread_mutex_init(&req->mutex, NULL);
    pthread_cond_init(&req->cond, NULL);
    req->done = false;
}

void applescreen_pending_request_destroy(applescreen_pending_request_t *req) {
    pthread_mutex_destroy(&req->mutex);
    pthread_cond_destroy(&req->cond);
}

bool applescreen_command_queue_submit(applescreen_pending_request_t *req) {
    pthread_mutex_lock(&g_ring_mutex);
    if (g_count == APPLESCREEN_QUEUE_CAPACITY) {
        pthread_mutex_unlock(&g_ring_mutex);
        applescreen_log("command_queue: full, dropping request_id=%u", req->command.request_id);
        return false;
    }
    g_ring[g_tail] = req;
    g_tail = (g_tail + 1) % APPLESCREEN_QUEUE_CAPACITY;
    g_count++;
    pthread_mutex_unlock(&g_ring_mutex);

    pthread_mutex_lock(&req->mutex);
    while (!req->done) {
        pthread_cond_wait(&req->cond, &req->mutex);
    }
    pthread_mutex_unlock(&req->mutex);
    return true;
}

void applescreen_command_queue_drain(void) {
    for (;;) {
        applescreen_pending_request_t *req = NULL;

        pthread_mutex_lock(&g_ring_mutex);
        if (g_count > 0) {
            req = g_ring[g_head];
            g_head = (g_head + 1) % APPLESCREEN_QUEUE_CAPACITY;
            g_count--;
        }
        pthread_mutex_unlock(&g_ring_mutex);

        if (!req) {
            break;
        }

        // Ring mutex is already released here - safe to call into
        // GLFW/AppKit from this dispatch without risking blocking a socket
        // thread that's only trying to push a new, unrelated request.
        applescreen_window_control_dispatch(&req->command, &req->response);

        pthread_mutex_lock(&req->mutex);
        req->done = true;
        pthread_cond_signal(&req->cond);
        pthread_mutex_unlock(&req->mutex);
    }
}
