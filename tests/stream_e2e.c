/* Runs the real multi-connection downloader on a PC against a real URL.
   usage: stream_e2e <url> <size-in-bytes> <out-dir/>
   The manual "stream-e2e" workflow compares the result with unzip's. */
#include "stream_fetch.h"
#include "applog.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

void appLog(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

void appLogSection(const char *title) { fprintf(stderr, "===== %s =====\n", title); }

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: %s <url> <size> <out-dir/>\n", argv[0]);
        return 2;
    }
    downloadGlobalInit();
    mkdir(argv[3], 0755);

    char reason[160];
    int rc = streamFetchZip(argv[1], atoll(argv[2]), argv[3], NULL, NULL,
                            reason, sizeof(reason));
    if (rc != 0) fprintf(stderr, "FAILED: %s\n", reason);
    downloadGlobalCleanup();
    return rc == 0 ? 0 : 1;
}
