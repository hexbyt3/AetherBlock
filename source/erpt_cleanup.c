#include "erpt_cleanup.h"
#include "applog.h"
#include <switch.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define ERPT_REPORTS_DIR "/atmosphere/erpt_reports"

/* Atmosphère saves every error report to the SD card (it always redirects
   them; there is no setting to stop that), and a bot racks up hundreds a day
   from trade-communication errors alone. At boot, Atmosphère's erpt deletes
   the whole folder once it holds 1000+ reports -- before erpt starts, so the
   console sits at the Nintendo logo for as long as that takes. With a couple
   thousand reports on exFAT it takes long enough to look like a hang (seen
   2026-09-29: 2695 reports, powered off with 1841 still left). Clearing them
   here, before any reboot we trigger, keeps that pass from ever running. */
int erptCleanupReports(void) {
    DIR *d = opendir(ERPT_REPORTS_DIR);
    if (!d)
        return 0;

    int removed = 0, failed = 0;
    struct dirent *entry;
    char path[512];
    while ((entry = readdir(d)) != NULL) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
            continue;
        snprintf(path, sizeof(path), "%s/%s", ERPT_REPORTS_DIR, entry->d_name);
        struct stat st;
        if (stat(path, &st) != 0 || S_ISDIR(st.st_mode))
            continue;
        if (remove(path) == 0)
            removed++;
        else
            failed++;
    }
    closedir(d);
    fsdevCommitDevice("sdmc");

    if (removed || failed)
        appLog("erpt: removed %d saved error report(s)%s", removed,
               failed ? " (some could not be removed)" : "");
    return removed;
}
