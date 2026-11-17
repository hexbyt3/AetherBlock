/* Runs the real network code on a PC against real URLs.
   usage: stream_e2e <url> <size-in-bytes> <out-dir/>   multi-connection download
          stream_e2e --resolve <url>                    print where redirects end
   The "stream-e2e" CI job compares the download with unzip's output. */
#include "stream_fetch.h"
#include "applog.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
    if (argc == 3 && strcmp(argv[1], "--resolve") == 0) {
        char landed[512];
        downloadGlobalInit();
        int rc = downloadResolveUrl(argv[2], landed, sizeof(landed));
        downloadGlobalCleanup();
        if (rc != 0) {
            fprintf(stderr, "FAILED to resolve %s\n", argv[2]);
            return 1;
        }
        puts(landed);
        return 0;
    }

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
