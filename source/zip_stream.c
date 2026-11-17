#include "zip_stream.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <zlib.h>

enum { PH_HEADER, PH_SKIP, PH_DATA, PH_GAP, PH_DONE };

static long long now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static uint16_t rd16(const unsigned char *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool name_is_safe(const char *n) {
    if (n[0] == '\0' || n[0] == '/') return false;
    if (strchr(n, '\\') || strchr(n, ':')) return false;
    for (const char *p = n; *p; ) {
        const char *slash = strchr(p, '/');
        size_t seg = slash ? (size_t)(slash - p) : strlen(p);
        if (seg == 2 && p[0] == '.' && p[1] == '.') return false;
        if (!slash) break;
        p = slash + 1;
    }
    return true;
}

static int by_offset(const void *a, const void *b) {
    const ZsEntry *x = a, *y = b;
    return (x->hdr_off > y->hdr_off) - (x->hdr_off < y->hdr_off);
}

int zsParseIndex(const unsigned char *tail, size_t tail_len,
                 long long zip_size, ZsIndex *out) {
    memset(out, 0, sizeof(*out));
    if (tail_len < 22 || zip_size < (long long)tail_len) return -1;

    long long base = zip_size - (long long)tail_len;   /* file offset of tail[0] */
    size_t eocd = tail_len - 22;
    for (;; eocd--) {
        if (rd32(tail + eocd) == 0x06054b50) break;
        if (eocd == 0) return -1;
    }

    const unsigned char *e = tail + eocd;
    unsigned count = rd16(e + 10);
    long long cd_size = rd32(e + 12), cd_off = rd32(e + 16);
    if (rd16(e + 4) != 0 || rd16(e + 6) != 0 || rd16(e + 8) != count) return -1;
    if (count == 0 || count == 0xFFFF || cd_off == 0xFFFFFFFFLL) return -1;   /* zip64 */
    if (cd_off < base || cd_off + cd_size > base + (long long)eocd) return -1;

    ZsEntry *list = calloc(count, sizeof(*list));
    if (!list) return -1;

    const unsigned char *p = tail + (cd_off - base);
    const unsigned char *end = p + cd_size;
    for (unsigned i = 0; i < count; i++) {
        if (end - p < 46 || rd32(p) != 0x02014b50) goto bad;
        unsigned flags = rd16(p + 8), method = rd16(p + 10);
        unsigned nlen = rd16(p + 28), xlen = rd16(p + 30), clen = rd16(p + 32);
        long long csize = rd32(p + 20), usize = rd32(p + 24), lho = rd32(p + 42);
        if ((size_t)(end - p) < 46 + (size_t)nlen + xlen + clen) goto bad;
        if ((flags & 1) || method != 0 || csize != usize) goto bad;
        if (usize == 0xFFFFFFFFLL || lho == 0xFFFFFFFFLL) goto bad;
        if (nlen == 0 || nlen >= ZS_NAME_MAX) goto bad;

        ZsEntry *z = &list[i];
        memcpy(z->name, p + 46, nlen);
        z->name[nlen] = '\0';
        if (strlen(z->name) != nlen || !name_is_safe(z->name)) goto bad;
        z->is_dir = z->name[nlen - 1] == '/';
        if (z->is_dir && usize != 0) goto bad;
        z->crc = rd32(p + 16);
        z->size = usize;
        z->hdr_off = lho;
        p += 46 + nlen + xlen + clen;
    }

    qsort(list, count, sizeof(*list), by_offset);
    for (unsigned i = 0; i < count; i++) {
        ZsEntry *z = &list[i];
        z->end_off = (i + 1 < count) ? list[i + 1].hdr_off : cd_off;
        if (z->hdr_off + 30 + (long long)strlen(z->name) + z->size > z->end_off) goto bad;
    }

    out->entries = list;
    out->count = (int)count;
    out->cd_off = cd_off;
    return 0;

bad:
    free(list);
    return -1;
}

void zsFreeIndex(ZsIndex *idx) {
    free(idx->entries);
    memset(idx, 0, sizeof(*idx));
}

static void make_parent_dirs(char *path) {
    for (char *p = path + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(path, 0755);
            *p = '/';
        }
    }
}

static int fail(ZsSink *s, const char *what) {
    snprintf(s->err, sizeof(s->err), "%s: %.100s", what,
             s->cur <= s->last ? s->idx->entries[s->cur].name : "end of range");
    return -1;
}

void zsSinkBegin(ZsSink *s, const ZsIndex *idx, const char *dest,
                 int first, int last, char *iobuf, size_t iobuf_size) {
    memset(s, 0, sizeof(*s));
    s->idx = idx;
    s->dest = dest;
    s->cur = first;
    s->last = last;
    s->iobuf = iobuf;
    s->iobuf_size = iobuf_size;
    s->phase = PH_HEADER;
}

static int close_file(ZsSink *s) {
    FILE *fp = s->fp;
    s->fp = NULL;
    return fp ? fclose(fp) : 0;
}

static void enter_gap(ZsSink *s, long long pos) {
    s->left = s->idx->entries[s->cur].end_off - pos;
    s->phase = PH_GAP;
}

static int finish_entry(ZsSink *s) {
    const ZsEntry *z = &s->idx->entries[s->cur];
    long long t0 = now_ns();
    int closed = close_file(s);
    s->io_ns += now_ns() - t0;
    if (closed != 0) return fail(s, "write failed");
    if (s->crc != z->crc) {
        remove(s->path);
        return fail(s, "checksum mismatch");
    }
    return 0;
}

/* the fixed part of the local header is in; work out what follows it */
static int begin_entry(ZsSink *s) {
    const ZsEntry *z = &s->idx->entries[s->cur];
    if (rd32(s->hdr) != 0x04034b50 || rd16(s->hdr + 26) != strlen(z->name))
        return fail(s, "bad local header");

    long long skip = (long long)rd16(s->hdr + 26) + rd16(s->hdr + 28);
    long long data_off = z->hdr_off + 30 + skip;
    if (data_off + z->size > z->end_off) return fail(s, "entry overruns its slot");

    snprintf(s->path, sizeof(s->path), "%s%s", s->dest, z->name);
    make_parent_dirs(s->path);
    if (z->is_dir) {
        s->path[strlen(s->path) - 1] = '\0';
        mkdir(s->path, 0755);
    } else {
        long long t0 = now_ns();
        s->fp = fopen(s->path, "wb");
        if (!s->fp) return fail(s, "cannot create");
        if (s->iobuf) setvbuf(s->fp, s->iobuf, _IOFBF, s->iobuf_size);
        /* size the file up front so the card allocates it in one go rather
           than growing it a write at a time; harmless if it is refused */
        if (z->size > 0) {
            int rc = ftruncate(fileno(s->fp), (off_t)z->size);
            (void)rc;
        }
        s->io_ns += now_ns() - t0;
        s->crc = (uint32_t)crc32(0L, Z_NULL, 0);
    }

    s->left = skip;
    s->phase = PH_SKIP;
    return 0;
}

/* move past phases that need no more bytes */
static int advance(ZsSink *s) {
    for (;;) {
        const ZsEntry *z = &s->idx->entries[s->cur];
        if (s->phase == PH_SKIP && s->left == 0) {
            s->left = z->size;
            s->phase = PH_DATA;
        } else if (s->phase == PH_DATA && s->left == 0) {
            if (!z->is_dir && finish_entry(s) != 0) return -1;
            long long skip = (long long)rd16(s->hdr + 26) + rd16(s->hdr + 28);
            enter_gap(s, z->hdr_off + 30 + skip + z->size);
        } else if (s->phase == PH_GAP && s->left == 0) {
            s->cur++;
            s->hdr_have = 0;
            s->phase = s->cur > s->last ? PH_DONE : PH_HEADER;
            return 0;
        } else {
            return 0;
        }
    }
}

int zsSinkFeed(ZsSink *s, const unsigned char *data, size_t len) {
    while (len > 0) {
        if (s->phase == PH_DONE) return fail(s, "unexpected extra data");

        if (s->phase == PH_HEADER) {
            size_t n = sizeof(s->hdr) - (size_t)s->hdr_have;
            if (n > len) n = len;
            memcpy(s->hdr + s->hdr_have, data, n);
            s->hdr_have += (int)n;
            data += n;
            len -= n;
            if (s->hdr_have < (int)sizeof(s->hdr)) break;
            if (begin_entry(s) != 0) return -1;
        } else {
            size_t n = (long long)len > s->left ? (size_t)s->left : len;
            if (s->phase == PH_DATA && n > 0) {
                long long t0 = now_ns();
                if (fwrite(data, 1, n, s->fp) != n) return fail(s, "write failed");
                long long t1 = now_ns();
                s->crc = (uint32_t)crc32(s->crc, data, (uInt)n);
                s->io_ns += t1 - t0;
                s->crc_ns += now_ns() - t1;
            }
            s->left -= (long long)n;
            data += n;
            len -= n;
        }

        if (advance(s) != 0) return -1;
    }
    return 0;
}

int zsSinkFinish(ZsSink *s) {
    if (s->err[0]) return -1;
    if (s->phase != PH_DONE) return fail(s, "transfer ended early");
    return 0;
}

void zsSinkAbort(ZsSink *s) {
    if (s->fp) {
        close_file(s);
        remove(s->path);
    }
}
