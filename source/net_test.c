#include "net_test.h"
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>

#define TEST_PORT       443
#define CONNECT_TIMEOUT_MS 2000

void netTestPrepare(NetTestState *ts, HostsFile *hf) {
    memset(ts, 0, sizeof(*ts));

    for (int i = 0; i < hf->entry_count && ts->count < NET_TEST_MAX; i++) {
        HostEntry *e = &hf->entries[i];
        if (e->is_comment || e->is_blank) continue;
        if (e->hostname[0] == '\0') continue;

        strncpy(ts->entries[ts->count].hostname, e->hostname,
                sizeof(ts->entries[ts->count].hostname) - 1);
        ts->entries[ts->count].result = TEST_PENDING;
        ts->entries[ts->count].latency_ms = -1;
        ts->count++;
    }

    ts->current = 0;
    ts->running = true;
    ts->finished = false;
}

bool netTestHostname(const char *hostname, int *out_latency_ms) {
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    u64 start = armGetSystemTick();

    int ret = getaddrinfo(hostname, "443", &hints, &res);
    if (ret != 0 || !res) {
        if (res) freeaddrinfo(res);
        if (out_latency_ms) *out_latency_ms = -1;
        return false;
    }

    int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) {
        freeaddrinfo(res);
        if (out_latency_ms) *out_latency_ms = -1;
        return false;
    }

    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    int conn = connect(sock, res->ai_addr, res->ai_addrlen);
    bool connected = false;

    if (conn == 0) {
        connected = true;
    } else if (errno == EINPROGRESS) {
        struct pollfd pfd;
        pfd.fd = sock;
        pfd.events = POLLOUT;
        pfd.revents = 0;

        int pr = poll(&pfd, 1, CONNECT_TIMEOUT_MS);
        if (pr > 0 && (pfd.revents & POLLOUT)) {
            int so_error = 0;
            socklen_t len = sizeof(so_error);
            getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_error, &len);
            connected = (so_error == 0);
        }
    }

    close(sock);
    freeaddrinfo(res);

    u64 end = armGetSystemTick();
    int elapsed_ms = (int)((end - start) / 19200);

    if (out_latency_ms) *out_latency_ms = connected ? elapsed_ms : -1;
    return connected;
}

void netTestStep(NetTestState *ts) {
    if (!ts->running || ts->finished) return;
    if (ts->current >= ts->count) {
        ts->running = false;
        ts->finished = true;
        return;
    }

    NetTestEntry *e = &ts->entries[ts->current];
    e->result = TEST_RUNNING;

    int latency = -1;
    bool reachable = netTestHostname(e->hostname, &latency);

    e->latency_ms = latency;
    e->result = reachable ? TEST_REACHABLE : TEST_BLOCKED;

    ts->current++;
    if (ts->current >= ts->count) {
        ts->running = false;
        ts->finished = true;
    }
}
