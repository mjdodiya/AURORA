#include "aurora/daemon.h"

#include <stdio.h>
#include <string.h>

#if defined(__unix__) || defined(__APPLE__)
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

#if defined(__unix__) || defined(__APPLE__)
static int read_full(int fd, void *buf, size_t len)
{
    char *cursor = (char *)buf;
    size_t remaining = len;
    while (remaining > 0u) {
        ssize_t got = read(fd, cursor, remaining);
        if (got == 0) {
            return 0;
        }
        if (got < 0) {
            return -1;
        }
        cursor += got;
        remaining -= (size_t)got;
    }
    return 1;
}

static int write_full(int fd, const void *buf, size_t len)
{
    const char *cursor = (const char *)buf;
    size_t remaining = len;
    while (remaining > 0u) {
        ssize_t written = write(fd, cursor, remaining);
        if (written <= 0) {
            return -1;
        }
        cursor += written;
        remaining -= (size_t)written;
    }
    return 0;
}

static int run_unix_daemon(const char *path)
{
    struct aurora_daemon daemon;
    aurora_daemon_init(&daemon);

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return 1;
    }

    (void)unlink(path);

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    (void)snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        perror("bind");
        close(fd);
        return 1;
    }

    if (listen(fd, 16) != 0) {
        perror("listen");
        close(fd);
        return 1;
    }

    printf("aurora_daemon listening on %s\n", path);
    for (;;) {
        int client_fd = accept(fd, NULL, NULL);
        if (client_fd < 0) {
            continue;
        }

        for (;;) {
            struct aurora_ail_packet packet;
            int read_status = read_full(client_fd, &packet, sizeof(packet));
            if (read_status <= 0) {
                break;
            }

            struct aurora_policy_state state;
            (void)aurora_daemon_ingest_packet(&daemon, &packet, &state);
            if (write_full(client_fd, &state, sizeof(state)) != 0) {
                break;
            }
        }

        close(client_fd);
    }
}
#endif

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "/tmp/aurora.sock";

#if defined(__unix__) || defined(__APPLE__)
    signal(SIGPIPE, SIG_IGN);
    return run_unix_daemon(path);
#else
    (void)path;
    puts("aurora_daemon Unix-domain socket mode is available on Linux/WSL2.");
    puts("On this platform, run aurora_sample_apps.exe for the in-process AIL demo.");
    return 0;
#endif
}

