#ifndef NET_TEST_H
#define NET_TEST_H

/**
 * @file net_test.h
 * @brief Network connectivity testing for blocked hostnames.
 *
 * Allows the user to verify that hosts-file entries are actually being
 * blocked by attempting connections and reporting reachability / latency.
 */

#include <switch.h>
#include <stdbool.h>
#include "hosts_parser.h"

/** Result state of an individual hostname test. */
typedef enum {
    TEST_PENDING = 0, /**< Test has not started yet */
    TEST_RUNNING,     /**< Test is currently in progress */
    TEST_BLOCKED,     /**< Connection was blocked (expected for active rules) */
    TEST_REACHABLE,   /**< Host is reachable (rule may not be effective) */
    TEST_ERROR,       /**< An error occurred during the test */
} TestResult;

/** Data for a single hostname connectivity test. */
typedef struct {
    char hostname[192]; /**< Hostname under test */
    TestResult result;  /**< Current result status */
    int latency_ms;     /**< Round-trip latency in ms, or -1 if blocked/error */
} NetTestEntry;

/** Maximum number of hostnames that can be tested in one batch. */
#define NET_TEST_MAX 32

/** State for a batch of network connectivity tests. */
typedef struct {
    NetTestEntry entries[NET_TEST_MAX]; /**< Array of test entries */
    int count;                          /**< Total number of entries to test */
    int current;                        /**< Index of the entry currently being tested */
    bool running;                       /**< true while the batch is in progress */
    bool finished;                      /**< true once all tests have completed */
} NetTestState;

/** Populate the test list from the enabled entries in a hosts file. */
void netTestPrepare(NetTestState *ts, HostsFile *hf);

/**
 * Run one test step; call once per frame while running.
 * Note: blocks for up to ~2s per host (DNS + TCP connect timeout).
 */
void netTestStep(NetTestState *ts);

/** Test a single hostname. Returns true if the host is reachable. */
bool netTestHostname(const char *hostname, int *out_latency_ms);

#endif
