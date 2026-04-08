#include "pending.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int already_listed(const char *path) {
    FILE *fp = fopen(PENDING_LIST_PATH, "r");
    if (!fp) return 0;

    char line[1024];
    size_t tlen = strlen(path);
    int found = 0;
    while (fgets(line, sizeof(line), fp)) {
        size_t ll = strlen(line);
        while (ll > 0 && (line[ll - 1] == '\n' || line[ll - 1] == '\r'))
            line[--ll] = '\0';
        if (ll == tlen && memcmp(line, path, tlen) == 0) {
            found = 1;
            break;
        }
    }
    fclose(fp);
    return found;
}

int pendingAdd(const char *target_path) {
    if (!target_path || !*target_path) return -1;

    mkdir(AETHERBLOCK_CONFIG_DIR, 0755);

    if (already_listed(target_path)) return 0;

    FILE *fp = fopen(PENDING_LIST_PATH, "a");
    if (!fp) return -1;
    fprintf(fp, "%s\n", target_path);
    fclose(fp);
    return 0;
}

int pendingApply(void) {
    FILE *fp = fopen(PENDING_LIST_PATH, "r");
    if (!fp) return 0;

    /* collect entries in memory so we can rewrite the file cleanly */
    char **entries = NULL;
    int cap = 0, count = 0;
    char line[1024];

    while (fgets(line, sizeof(line), fp)) {
        size_t ll = strlen(line);
        while (ll > 0 && (line[ll - 1] == '\n' || line[ll - 1] == '\r'))
            line[--ll] = '\0';
        if (ll == 0) continue;

        if (count == cap) {
            int ncap = cap ? cap * 2 : 16;
            char **ne = realloc(entries, ncap * sizeof(char *));
            if (!ne) break;
            entries = ne;
            cap = ncap;
        }
        entries[count] = strdup(line);
        if (!entries[count]) break;
        count++;
    }
    fclose(fp);

    int succeeded = 0;
    int still_pending = 0;
    char **remaining = count > 0 ? calloc(count, sizeof(char *)) : NULL;

    for (int i = 0; i < count; i++) {
        const char *target = entries[i];
        char src[1200];
        snprintf(src, sizeof(src), "%s%s", target, PENDING_SUFFIX);

        struct stat st;
        if (stat(src, &st) != 0) {
            /* staged file missing — nothing we can do, drop from list */
            continue;
        }

        /* remove the existing target (if present) so rename can land.
           if target is locked, remove() will fail and rename() will
           likely fail too — in that case we keep the entry. */
        remove(target);
        if (rename(src, target) == 0) {
            succeeded++;
        } else {
            remaining[still_pending++] = entries[i];
            entries[i] = NULL;
        }
    }

    /* rewrite the pending file with whatever we couldn't swap */
    if (still_pending > 0) {
        FILE *out = fopen(PENDING_LIST_PATH, "w");
        if (out) {
            for (int i = 0; i < still_pending; i++)
                fprintf(out, "%s\n", remaining[i]);
            fclose(out);
        }
    } else {
        remove(PENDING_LIST_PATH);
    }

    for (int i = 0; i < count; i++)
        free(entries[i]);
    free(entries);
    free(remaining);

    return succeeded;
}

int pendingHasEntries(void) {
    struct stat st;
    if (stat(PENDING_LIST_PATH, &st) != 0) return 0;
    return st.st_size > 0;
}
