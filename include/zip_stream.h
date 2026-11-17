#ifndef ZIP_STREAM_H
#define ZIP_STREAM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Unpacks a zip whose entries are all stored (no compression) straight from
   the byte stream, with no copy of the archive on disk. The firmware packages
   are built this way, so each file can go to the card as it arrives and
   several byte ranges can be fetched at once. Nothing here touches the network
   or libnx, so tests/zip_stream_test.c runs it on the build machine. */

#define ZS_NAME_MAX 256

typedef struct {
    char      name[ZS_NAME_MAX];
    uint32_t  crc;
    long long size;
    long long hdr_off;   /* where this entry's local header starts */
    long long end_off;   /* where the next header (or the directory) starts */
    bool      is_dir;
} ZsEntry;

typedef struct {
    ZsEntry  *entries;   /* sorted by hdr_off */
    int       count;
    long long cd_off;    /* start of the central directory = end of file data */
} ZsIndex;

/* Build the index from the last tail_len bytes of a zip_size-byte archive.
   Returns 0, or -1 when the archive can't be streamed (compressed or
   encrypted entries, zip64, unsafe names, directory not inside the tail). */
int  zsParseIndex(const unsigned char *tail, size_t tail_len,
                  long long zip_size, ZsIndex *out);
void zsFreeIndex(ZsIndex *idx);

typedef struct {
    const ZsIndex *idx;
    const char    *dest;      /* output directory, ends with '/' */
    int            cur, last; /* entries [cur, last] are expected, in order */
    int            phase;
    unsigned char  hdr[30];
    int            hdr_have;
    long long      left;      /* bytes left in the current phase */
    FILE          *fp;
    char           path[ZS_NAME_MAX + 256];
    uint32_t       crc;
    char          *iobuf;
    size_t         iobuf_size;
    char           err[160];
    long long      io_ns;     /* time spent creating, writing and closing files */
    long long      crc_ns;    /* time spent checksumming */
} ZsSink;

/* Start consuming the bytes [entries[first].hdr_off, entries[last].end_off).
   iobuf (optional) becomes the stdio buffer of each output file. */
void zsSinkBegin(ZsSink *s, const ZsIndex *idx, const char *dest,
                 int first, int last, char *iobuf, size_t iobuf_size);
/* Returns 0, or -1 with s->err set. */
int  zsSinkFeed(ZsSink *s, const unsigned char *data, size_t len);
/* 0 when every expected entry arrived whole and passed its CRC. */
int  zsSinkFinish(ZsSink *s);
/* Close and delete the file being written, if any. */
void zsSinkAbort(ZsSink *s);

#endif
