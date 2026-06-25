#include "applog.h"
#include "config.h"
#include <stdio.h>
#include <stdarg.h>
#include <sys/stat.h>

void appLog(const char *fmt, ...) {
    mkdir(AETHERBLOCK_CONFIG_DIR, 0755);
    FILE *fp = fopen(APP_LOG_PATH, "a");
    if (!fp) return;

    va_list ap;
    va_start(ap, fmt);
    vfprintf(fp, fmt, ap);
    va_end(ap);
    fputc('\n', fp);
    fclose(fp);
}

void appLogSection(const char *title) {
    mkdir(AETHERBLOCK_CONFIG_DIR, 0755);

    /* rotate once the log gets large so it never grows without bound */
    const char *mode = "a";
    struct stat st;
    if (stat(APP_LOG_PATH, &st) == 0 && st.st_size > APP_LOG_MAX_SIZE)
        mode = "w";

    FILE *fp = fopen(APP_LOG_PATH, mode);
    if (!fp) return;
    fprintf(fp, "\n===== %s =====\n", title);
    fclose(fp);
}
