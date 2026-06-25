#ifndef EXTRACT_H
#define EXTRACT_H

#include <stddef.h>

/* Negative return values from extractZip — hard failures that abort the
   whole operation before any file is written. A non-negative return is
   the count of individual entries that failed mid-extraction. */
enum {
    EXTRACT_ERR_OPEN   = -1,  /* zip could not be opened (corrupt/truncated) */
    EXTRACT_ERR_INDEX  = -2,  /* archive directory could not be read */
    EXTRACT_ERR_MEMORY = -3,  /* out of memory for the copy buffer */
};

typedef void (*ExtractProgressCb)(int current, int total, const char *filename, void *userdata);

/* If stage_locked_files is non-zero, any file that can't be written
   in place (locked by a running sysmodule, the app's own NRO, etc.)
   is written to <path>.ab_new instead and appended to the pending list
   for later swap. Such files are NOT counted as errors. */
int extractZip(const char *zip_path, const char *dest_path,
               const char **preserve_prefixes, int preserve_count,
               ExtractProgressCb cb, void *userdata,
               char *failed_out, size_t failed_out_size,
               int stage_locked_files);

int removeDir(const char *path);

int ensureDirForFile(const char *filepath);

#endif
