#ifndef AURORA_IPC_H
#define AURORA_IPC_H

#include "aurora/types.h"

struct aurora_ail_packet;
struct aurora_policy_state;

#define AURORA_IPC_PATH_MAX 108u

struct aurora_ipc_endpoint {
    int fd;
    bool connected;
    char path[AURORA_IPC_PATH_MAX];
};

void aurora_ipc_endpoint_init(struct aurora_ipc_endpoint *endpoint);
enum aurora_status aurora_ipc_connect_unix(
    struct aurora_ipc_endpoint *endpoint,
    const char *path);
enum aurora_status aurora_ipc_roundtrip(
    struct aurora_ipc_endpoint *endpoint,
    const struct aurora_ail_packet *packet,
    struct aurora_policy_state *state);
void aurora_ipc_close(struct aurora_ipc_endpoint *endpoint);

#endif

