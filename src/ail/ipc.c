#include "aurora/ail.h"
#include "aurora/ipc.h"

#include <stdio.h>
#include <string.h>

#if defined(__unix__) || defined(__APPLE__)
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

void aurora_ipc_endpoint_init(struct aurora_ipc_endpoint *endpoint)
{
    if (endpoint == NULL) {
        return;
    }
    memset(endpoint, 0, sizeof(*endpoint));
    endpoint->fd = -1;
}

#if defined(__unix__) || defined(__APPLE__)
static enum aurora_status write_full(int fd, const void *buf, size_t len)
{
    const char *cursor = (const char *)buf;
    size_t remaining = len;
    while (remaining > 0u) {
        ssize_t written = write(fd, cursor, remaining);
        if (written <= 0) {
            return AURORA_ERR_INVALID;
        }
        cursor += written;
        remaining -= (size_t)written;
    }
    return AURORA_OK;
}

static enum aurora_status read_full(int fd, void *buf, size_t len)
{
    char *cursor = (char *)buf;
    size_t remaining = len;
    while (remaining > 0u) {
        ssize_t got = read(fd, cursor, remaining);
        if (got <= 0) {
            return AURORA_ERR_INVALID;
        }
        cursor += got;
        remaining -= (size_t)got;
    }
    return AURORA_OK;
}
#endif

enum aurora_status aurora_ipc_connect_unix(
    struct aurora_ipc_endpoint *endpoint,
    const char *path)
{
    if (endpoint == NULL || path == NULL) {
        return AURORA_ERR_INVALID;
    }

#if defined(__unix__) || defined(__APPLE__)
    aurora_ipc_endpoint_init(endpoint);

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return AURORA_ERR_INVALID;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    (void)snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);

    if (connect(fd, (const struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return AURORA_ERR_NOT_FOUND;
    }

    endpoint->fd = fd;
    endpoint->connected = true;
    (void)snprintf(endpoint->path, sizeof(endpoint->path), "%s", path);
    return AURORA_OK;
#else
    (void)endpoint;
    (void)path;
    return AURORA_ERR_DENIED;
#endif
}

enum aurora_status aurora_ipc_roundtrip(
    struct aurora_ipc_endpoint *endpoint,
    const struct aurora_ail_packet *packet,
    struct aurora_policy_state *state)
{
    if (endpoint == NULL || packet == NULL || state == NULL || !endpoint->connected) {
        return AURORA_ERR_INVALID;
    }

#if defined(__unix__) || defined(__APPLE__)
    enum aurora_status status = write_full(endpoint->fd, packet, sizeof(*packet));
    if (status != AURORA_OK) {
        return status;
    }
    return read_full(endpoint->fd, state, sizeof(*state));
#else
    (void)endpoint;
    (void)packet;
    (void)state;
    return AURORA_ERR_DENIED;
#endif
}

void aurora_ipc_close(struct aurora_ipc_endpoint *endpoint)
{
    if (endpoint == NULL) {
        return;
    }

#if defined(__unix__) || defined(__APPLE__)
    if (endpoint->fd >= 0) {
        close(endpoint->fd);
    }
#endif

    aurora_ipc_endpoint_init(endpoint);
}

