#ifndef NET_TEST_H
#define NET_TEST_H

#include <switch.h>
#include <stdbool.h>
#include "hosts_parser.h"

typedef enum {
    TEST_PENDING = 0,
    TEST_RUNNING,
    TEST_BLOCKED,
    TEST_REACHABLE,
    TEST_ERROR,
} TestResult;

typedef struct {
    char hostname[192];
    TestResult result;
    int latency_ms;
} NetTestEntry;

#define NET_TEST_MAX 32

typedef struct {
    NetTestEntry entries[NET_TEST_MAX];
    int count;
    int current;
    bool running;
    bool finished;
} NetTestState;

void netTestPrepare(NetTestState *ts, HostsFile *hf);
void netTestStep(NetTestState *ts);
bool netTestHostname(const char *hostname, int *out_latency_ms);

#endif
