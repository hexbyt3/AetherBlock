#include "extract.h"
#include <minizip/unzip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

#define EXTRACT_BUF_SIZE (256 * 1024)

static bool has_path_traversal(const char *name) {
    if (strcmp(name, "..") == 0) return true;
    if (strncmp(name, "../", 3) == 0) return true;
    if (strstr(name, "/../") != NULL) return true;
    size_t len = strlen(name);
    if (len >= 3 && strcmp(name + len - 3, "/..") == 0) return true;
    return false;
}

static bool should_preserve(const char *entry_path,
                            const char **prefixes, int prefix_count) {
    for (int i = 0; i < prefix_count; i++) {
        size_t plen = strlen(prefixes[i]);
        if (strncmp(entry_path, prefixes[i], plen) == 0) {
            char next = entry_path[plen];
            if (next == '\0' || next == '/')
                return true;
        }
    }
    return false;
}

int ensureDirForFile(const char *filepath) {
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", filepath);

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    return 0;
}

static int ensure_dir(const char *dir) {
    ensureDirForFile(dir);
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", dir);
    size_t len = strlen(tmp);
    if (len > 0 && tmp[len - 1] == '/')
        tmp[len - 1] = '\0';
    mkdir(tmp, 0755);
    return 0;
}

int extractZip(const char *zip_path, const char *dest_path,
               const char **preserve_prefixes, int preserve_count,
               ExtractProgressCb cb, void *userdata) {
    unzFile zf = unzOpen(zip_path);
    if (!zf) return -1;

    unz_global_info gi;
    if (unzGetGlobalInfo(zf, &gi) != UNZ_OK) {
        unzClose(zf);
        return -1;
    }

    int total = (int)gi.number_entry;
    char *buf = malloc(EXTRACT_BUF_SIZE);
    if (!buf) {
        unzClose(zf);
        return -1;
    }

    ensure_dir(dest_path);
    int errors = 0;

    for (int i = 0; i < total; i++) {
        char filename[512];
        unz_file_info fi;

        if (unzGetCurrentFileInfo(zf, &fi, filename, sizeof(filename),
                                  NULL, 0, NULL, 0) != UNZ_OK) {
            errors++;
            goto next_entry;
        }

        if (cb) cb(i + 1, total, filename, userdata);

        if (has_path_traversal(filename))
            goto next_entry;

        if (preserve_prefixes && should_preserve(filename, preserve_prefixes, preserve_count))
            goto next_entry;

        char full_path[1024];
        int written = snprintf(full_path, sizeof(full_path), "%s%s%s",
                               dest_path,
                               (dest_path[strlen(dest_path) - 1] == '/') ? "" : "/",
                               filename);
        if (written >= (int)sizeof(full_path))
            goto next_entry;

        size_t name_len = strlen(filename);
        if (name_len > 0 && filename[name_len - 1] == '/') {
            ensure_dir(full_path);
        } else {
            ensureDirForFile(full_path);

            if (unzOpenCurrentFile(zf) != UNZ_OK) {
                errors++;
                goto next_entry;
            }

            FILE *fp = fopen(full_path, "wb");
            if (!fp) {
                errors++;
                unzCloseCurrentFile(zf);
                goto next_entry;
            }

            int bytes;
            bool write_ok = true;
            while ((bytes = unzReadCurrentFile(zf, buf, EXTRACT_BUF_SIZE)) > 0) {
                if ((int)fwrite(buf, 1, bytes, fp) != bytes) {
                    write_ok = false;
                    break;
                }
            }
            if (bytes < 0) write_ok = false;
            fclose(fp);
            unzCloseCurrentFile(zf);

            if (!write_ok) {
                remove(full_path);
                errors++;
            }
        }

next_entry:
        if (i + 1 < total) unzGoToNextFile(zf);
    }

    free(buf);
    unzClose(zf);
    return errors;
}

int removeDir(const char *path) {
    DIR *d = opendir(path);
    if (!d) return -1;

    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        char full[1024];
        snprintf(full, sizeof(full), "%s/%s", path, entry->d_name);

        struct stat st;
        if (lstat(full, &st) == 0 && S_ISDIR(st.st_mode))
            removeDir(full);
        else
            remove(full);
    }
    closedir(d);
    return rmdir(path);
}
