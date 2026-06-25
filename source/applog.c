#include "applog.h"
#include "config.h"
#include <switch.h>
#include <stdio.h>
#include <stdarg.h>
#include <sys/stat.h>

/* The log is our only window into what happens right before a reboot, so it
   has to survive one. libnx never flushes SD writes on fclose, which means a
   line written and then followed by a reboot/brick is silently lost. Commit
   the card after every single log write so the log is a reliable black box
   even when the very next thing the console does is reboot into a brick. */
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

    fsdevCommitDevice("sdmc");
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

    fsdevCommitDevice("sdmc");
}
