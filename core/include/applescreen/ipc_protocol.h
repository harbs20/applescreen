// Canonical wire format shared verbatim between the injected core (C) and the
// control app (Swift, via shared/ipc/module.modulemap). Keep every field a
// fixed-width type - no pointers, no enums without an explicit underlying
// size - so the layout matches on both sides of the process boundary.
//
// Transport is AF_UNIX + SOCK_STREAM at APPLESCREEN_IPC_SOCKET_PATH (NOT
// SOCK_SEQPACKET - verified empirically that macOS's AF_UNIX implementation
// returns EPROTONOSUPPORT for SOCK_SEQPACKET, unlike Linux). Because every
// message here is a fixed-size POD, both sides just loop send/recv until
// exactly sizeof(applescreen_command_t) or sizeof(applescreen_response_t)
// bytes have moved - no separate length-prefix framing needed.
#ifndef APPLESCREEN_IPC_PROTOCOL_H
#define APPLESCREEN_IPC_PROTOCOL_H

#include <stdint.h>

#define APPLESCREEN_IPC_MAGIC 0x41504c53u /* "APLS" */
#define APPLESCREEN_IPC_VERSION 1u
#define APPLESCREEN_IPC_SOCKET_PATH "/tmp/applescreen.sock"

typedef enum {
    APPLESCREEN_CMD_PING = 1,
    APPLESCREEN_CMD_SET_WINDOW_POS = 2,
    APPLESCREEN_CMD_SET_WINDOW_SIZE = 3,
    APPLESCREEN_CMD_FOCUS_WINDOW = 4,
    APPLESCREEN_CMD_GET_WINDOW_POS = 5,
    APPLESCREEN_CMD_GET_WINDOW_SIZE = 6,
    APPLESCREEN_CMD_SET_WINDOW_SHOULD_CLOSE = 7,
} applescreen_cmd_type_t;

typedef enum {
    APPLESCREEN_STATUS_OK = 0,
    APPLESCREEN_STATUS_NO_WINDOW = 1,
    APPLESCREEN_STATUS_UNKNOWN_COMMAND = 2,
    APPLESCREEN_STATUS_BAD_VERSION = 3,
    APPLESCREEN_STATUS_QUEUE_FULL = 4,
} applescreen_status_t;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t request_id;
    int32_t type;  /* applescreen_cmd_type_t */
    int32_t arg1;  /* x / width / bool, per type */
    int32_t arg2;  /* y / height, per type */
} applescreen_command_t;

typedef struct {
    uint32_t magic;
    uint32_t request_id;
    int32_t status; /* applescreen_status_t */
    int32_t value1; /* x / width, for GET_WINDOW_POS / GET_WINDOW_SIZE */
    int32_t value2; /* y / height, for GET_WINDOW_POS / GET_WINDOW_SIZE */
} applescreen_response_t;

/* Tripwires: bump APPLESCREEN_IPC_VERSION and update both sides if these fire. */
_Static_assert(sizeof(applescreen_command_t) == 24, "applescreen_command_t layout changed");
_Static_assert(sizeof(applescreen_response_t) == 20, "applescreen_response_t layout changed");

#endif /* APPLESCREEN_IPC_PROTOCOL_H */
