#include "ipc_server.h"

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "applescreen/ipc_protocol.h"
#include "command_queue.h"
#include "log.h"

// AF_UNIX + SOCK_STREAM (SOCK_SEQPACKET is not supported for AF_UNIX on
// macOS - verified empirically: socket() returns EPROTONOSUPPORT). Since
// every message is a fixed-size POD, a stream socket only needs a loop that
// keeps reading/writing until exactly that many bytes have moved - no
// explicit length-prefix framing required.
static int recv_full(int fd, void *buf, size_t len) {
    size_t got = 0;
    while (got < len) {
        ssize_t n = recv(fd, (char *)buf + got, len - got, 0);
        if (n <= 0) {
            return (int)n;
        }
        got += (size_t)n;
    }
    return (int)got;
}

static int send_full(int fd, const void *buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, (const char *)buf + sent, len - sent, 0);
        if (n <= 0) {
            return (int)n;
        }
        sent += (size_t)n;
    }
    return (int)sent;
}

static void *connection_thread(void *arg) {
    int fd = (int)(intptr_t)arg;
    applescreen_log("ipc_server: connection accepted (fd=%d)", fd);

    for (;;) {
        applescreen_command_t command;
        int n = recv_full(fd, &command, sizeof(command));
        if (n != (int)sizeof(command)) {
            applescreen_log("ipc_server: connection closed or short read (fd=%d, n=%d, errno=%d)",
                             fd, n, errno);
            break;
        }

        applescreen_response_t response;
        if (command.magic != APPLESCREEN_IPC_MAGIC || command.version != APPLESCREEN_IPC_VERSION) {
            memset(&response, 0, sizeof(response));
            response.magic = APPLESCREEN_IPC_MAGIC;
            response.request_id = command.request_id;
            response.status = APPLESCREEN_STATUS_BAD_VERSION;
        } else {
            applescreen_pending_request_t req;
            applescreen_pending_request_init(&req, &command);
            if (applescreen_command_queue_submit(&req)) {
                response = req.response;
            } else {
                memset(&response, 0, sizeof(response));
                response.magic = APPLESCREEN_IPC_MAGIC;
                response.request_id = command.request_id;
                response.status = APPLESCREEN_STATUS_QUEUE_FULL;
            }
            applescreen_pending_request_destroy(&req);
        }

        if (send_full(fd, &response, sizeof(response)) != (int)sizeof(response)) {
            applescreen_log("ipc_server: failed to send response (fd=%d, errno=%d)", fd, errno);
            break;
        }
    }

    close(fd);
    return NULL;
}

static void *accept_loop(void *arg) {
    int listen_fd = (int)(intptr_t)arg;
    for (;;) {
        int client_fd = accept(listen_fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            applescreen_log("ipc_server: accept failed (errno=%d), stopping accept loop", errno);
            break;
        }

        pthread_t thread;
        if (pthread_create(&thread, NULL, connection_thread, (void *)(intptr_t)client_fd) != 0) {
            applescreen_log("ipc_server: failed to spawn connection thread");
            close(client_fd);
            continue;
        }
        pthread_detach(thread);
    }
    return NULL;
}

void applescreen_ipc_server_start(void) {
    int listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        applescreen_log("ipc_server: socket() failed (errno=%d)", errno);
        return;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, APPLESCREEN_IPC_SOCKET_PATH, sizeof(addr.sun_path) - 1);

    // Remove a stale socket file left behind by a previous run (e.g. the
    // dylib was killed rather than shut down cleanly) before binding.
    unlink(APPLESCREEN_IPC_SOCKET_PATH);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        applescreen_log("ipc_server: bind(%s) failed (errno=%d)", APPLESCREEN_IPC_SOCKET_PATH, errno);
        close(listen_fd);
        return;
    }

    if (listen(listen_fd, 4) != 0) {
        applescreen_log("ipc_server: listen() failed (errno=%d)", errno);
        close(listen_fd);
        return;
    }

    pthread_t thread;
    if (pthread_create(&thread, NULL, accept_loop, (void *)(intptr_t)listen_fd) != 0) {
        applescreen_log("ipc_server: failed to spawn accept loop thread");
        close(listen_fd);
        return;
    }
    pthread_detach(thread);

    applescreen_log("ipc_server: listening on %s", APPLESCREEN_IPC_SOCKET_PATH);
}
