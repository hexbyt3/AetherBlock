#ifndef STREAM_FETCH_H
#define STREAM_FETCH_H

#include "download.h"

/* Download a stored-only zip over several connections at once and unpack it
   into dest_dir (must end with '/') as the bytes arrive -- the archive itself
   never touches the card. Every file is checked against the zip's CRC.

   Returns 0 on success. On -1 nothing usable is left in dest_dir and reason
   says why; the caller can fall back to download-then-extract. */
int streamFetchZip(const char *url, long long zip_size, const char *dest_dir,
                   DownloadProgressCb cb, void *userdata,
                   char *reason, size_t reason_size);

#endif
