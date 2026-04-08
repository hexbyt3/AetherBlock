#include "pending.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int files_equal(const char *a, const char *b) {
    struct stat sa, sb;
    if (stat(a, &sa) != 0 || stat(b, &sb) != 0) return 0;
    if (sa.st_size != sb.st_size) return 0;
    if (sa.st_size == 0) return 1;

    FILE *fa = fopen(a, "rb");
    if (!fa) return 0;
    FILE *fb = fopen(b, "rb");
    if (!fb) { fclose(fa); return 0; }

    char buf_a[4096], buf_b[4096];
    int equal = 1;
    for (;;) {
        size_t na = fread(buf_a, 1, sizeof(buf_a), fa);
        size_t nb = fread(buf_b, 1, sizeof(buf_b), fb);
        if (na != nb) { equal = 0; break; }
        if (na == 0) break;
        if (memcmp(buf_a, buf_b, na) != 0) { equal = 0; break; }
    }
    fclose(fa);
    fclose(fb);
    return equal;
}

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

        /* cheap win: if the target already has the exact same bytes
           as the sidecar, the swap has effectively already happened
           (either via a prior successful rename, a stash fallback at
           extract time, or a manual copy). drop the redundant
           sidecar and move on. */
        if (files_equal(src, target)) {
            remove(src);
            succeeded++;
            continue;
        }

        /* stash the original to a backup path first, then try to
           rename the sidecar into place. if either step fails we can
           roll back, so the target is never lost. */
        char backup[1200];
        snprintf(backup, sizeof(backup), "%s.ab_bak", target);
        remove(backup);

        if (rename(target, backup) != 0) {
            /* target is locked against rename — keep the entry for a
               later retry (next reboot or next launch) */
            remaining[still_pending++] = entries[i];
            entries[i] = NULL;
            continue;
        }

        if (rename(src, target) == 0) {
            /* belt-and-suspenders: HOS rename semantics may not unlink
               the source on a successful rename in all cases */
            remove(src);
            remove(backup);
            succeeded++;
        } else {
            /* couldn't install the new file — put the original back */
            rename(backup, target);
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
