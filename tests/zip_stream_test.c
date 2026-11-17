/* Feeds hand-built stored zips through the streaming unpacker the way the
   network does: split across requests, in awkward chunk sizes. */
#include "zip_stream.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

#define OUT "zs_out/"

typedef struct {
    const char *name;
    size_t size;
    int descriptor;     /* sizes live after the data, not in the local header */
    int local_extra;    /* extra bytes only the local header carries */
} Spec;

static unsigned char zip[1 << 20];
static size_t zip_len;
static unsigned char *content[16];

static void w16(unsigned v) { zip[zip_len++] = v & 0xFF; zip[zip_len++] = (v >> 8) & 0xFF; }
static void w32(unsigned long v) { w16(v & 0xFFFF); w16((v >> 16) & 0xFFFF); }
static void wbytes(const void *p, size_t n) { memcpy(zip + zip_len, p, n); zip_len += n; }

static void build_zip(const Spec *specs, int n, unsigned method) {
    unsigned long off[16], crc[16];
    zip_len = 0;
    for (int i = 0; i < n; i++) {
        const Spec *s = &specs[i];
        free(content[i]);
        content[i] = malloc(s->size + 1);
        for (size_t k = 0; k < s->size; k++)
            content[i][k] = (unsigned char)(k * 31 + i * 7 + (k >> 8));
        crc[i] = crc32(0L, content[i], (uInt)s->size);
        off[i] = zip_len;

        w32(0x04034b50); w16(20); w16(s->descriptor ? 8 : 0); w16(method);
        w16(0); w16(0);
        w32(s->descriptor ? 0 : crc[i]);
        w32(s->descriptor ? 0 : s->size);
        w32(s->descriptor ? 0 : s->size);
        w16(strlen(s->name)); w16(s->local_extra);
        wbytes(s->name, strlen(s->name));
        for (int k = 0; k < s->local_extra; k++) zip[zip_len++] = 0xEE;
        wbytes(content[i], s->size);
        if (s->descriptor) { w32(0x08074b50); w32(crc[i]); w32(s->size); w32(s->size); }
    }

    unsigned long cd = zip_len;
    for (int i = 0; i < n; i++) {
        const Spec *s = &specs[i];
        w32(0x02014b50); w16(20); w16(20); w16(s->descriptor ? 8 : 0); w16(method);
        w16(0); w16(0); w32(crc[i]); w32(s->size); w32(s->size);
        w16(strlen(s->name)); w16(0); w16(0); w16(0); w16(0); w32(0); w32(off[i]);
        wbytes(s->name, strlen(s->name));
    }
    unsigned long cd_size = zip_len - cd;
    w32(0x06054b50); w16(0); w16(0); w16(n); w16(n); w32(cd_size); w32(cd); w16(0);
}

static const unsigned char *tail(size_t want, size_t *len) {
    *len = want < zip_len ? want : zip_len;
    return zip + zip_len - *len;
}

static void check_file(const char *name, const unsigned char *want, size_t size) {
    char path[512];
    snprintf(path, sizeof(path), OUT "%s", name);
    FILE *fp = fopen(path, "rb");
    assert(fp);
    unsigned char *got = malloc(size + 1);
    assert(fread(got, 1, size + 1, fp) == size);
    assert(memcmp(got, want, size) == 0);
    free(got);
    fclose(fp);
}

/* stream entries [first, last] in chunk-byte pieces; returns the sink result */
static int run(const ZsIndex *idx, int first, int last, size_t chunk, long long cut) {
    static char iobuf[4096];
    ZsSink s;
    zsSinkBegin(&s, idx, OUT, first, last, iobuf, sizeof(iobuf));
    long long a = idx->entries[first].hdr_off, b = idx->entries[last].end_off;
    if (cut >= 0) b = a + cut;
    for (long long p = a; p < b; p += (long long)chunk) {
        size_t n = (size_t)(b - p) < chunk ? (size_t)(b - p) : chunk;
        if (zsSinkFeed(&s, zip + p, n) != 0) { zsSinkAbort(&s); return -1; }
    }
    int rc = zsSinkFinish(&s);
    if (rc != 0) zsSinkAbort(&s);
    return rc;
}

int main(void) {
    const Spec specs[] = {
        { "dir/",            0,      0, 0 },
        { "dir/a.nca",       70000,  0, 0 },
        { "dir/sub/b.nca",   1,      0, 28 },
        { "empty.bin",       0,      0, 0 },
        { "c.cnmt.nca",      123457, 1, 4 },
        { "d.nca",           4096,   0, 0 },
    };
    const int n = (int)(sizeof(specs) / sizeof(specs[0]));
    mkdir(OUT, 0755);

    build_zip(specs, n, 0);
    size_t tl;
    const unsigned char *t = tail(64 * 1024, &tl);
    ZsIndex idx;
    assert(zsParseIndex(t, tl, (long long)zip_len, &idx) == 0);
    assert(idx.count == n);
    for (int i = 0; i < n; i++) {
        assert(strcmp(idx.entries[i].name, specs[i].name) == 0);
        assert(idx.entries[i].size == (long long)specs[i].size);
    }
    assert(idx.entries[n - 1].end_off == idx.cd_off);

    /* whole archive in one request, at chunk sizes that split every field */
    const size_t chunks[] = { 1, 7, 29, 30, 31, 4096, 16384, 1 << 20 };
    for (size_t c = 0; c < sizeof(chunks) / sizeof(chunks[0]); c++) {
        assert(run(&idx, 0, n - 1, chunks[c], -1) == 0);
        for (int i = 0; i < n; i++)
            if (specs[i].name[strlen(specs[i].name) - 1] != '/')
                check_file(specs[i].name, content[i], specs[i].size);
    }

    /* split across requests, fetched out of order */
    assert(run(&idx, 4, 5, 1000, -1) == 0);
    assert(run(&idx, 0, 1, 1000, -1) == 0);
    assert(run(&idx, 2, 3, 1000, -1) == 0);
    for (int i = 1; i < n; i++)
        check_file(specs[i].name, content[i], specs[i].size);

    /* a connection that drops mid-file fails and leaves no partial file */
    assert(run(&idx, 1, 1, 512, 30000) != 0);
    assert(access(OUT "dir/a.nca", F_OK) != 0);

    /* a flipped byte is caught by the checksum and the file is removed */
    size_t victim = (size_t)idx.entries[1].hdr_off + 30 + strlen(specs[1].name) + 500;
    zip[victim] ^= 0x40;
    assert(run(&idx, 1, 1, 512, -1) != 0);
    assert(access(OUT "dir/a.nca", F_OK) != 0);
    zip[victim] ^= 0x40;
    assert(run(&idx, 1, 1, 512, -1) == 0);

    /* data from the wrong offset (a server that ignored the range) */
    ZsSink s;
    zsSinkBegin(&s, &idx, OUT, 5, 5, NULL, 0);
    assert(zsSinkFeed(&s, zip, 4096) != 0);
    zsSinkAbort(&s);
    /* ...even when a same-length name lets the header pass: the checksum */
    zsSinkBegin(&s, &idx, OUT, 5, 5, NULL, 0);
    memcpy(zip + idx.entries[5].hdr_off + 30 + 5, content[1], 4096);
    assert(zsSinkFeed(&s, zip + idx.entries[5].hdr_off,
                      (size_t)(idx.entries[5].end_off - idx.entries[5].hdr_off)) != 0);
    zsSinkAbort(&s);
    assert(access(OUT "d.nca", F_OK) != 0);

    /* more bytes than the range should hold */
    zsSinkBegin(&s, &idx, OUT, 3, 3, NULL, 0);
    assert(zsSinkFeed(&s, zip + idx.entries[3].hdr_off,
                      (size_t)(idx.entries[4].end_off - idx.entries[3].hdr_off)) != 0);
    zsSinkAbort(&s);
    zsFreeIndex(&idx);

    /* archives the fast path must hand back to the regular extractor */
    assert(zsParseIndex(t, 21, (long long)zip_len, &idx) != 0);          /* no directory */
    t = tail(40, &tl);
    assert(zsParseIndex(t, tl, (long long)zip_len, &idx) != 0);          /* directory cut off */

    build_zip(specs, n, 8);                                              /* deflated */
    t = tail(64 * 1024, &tl);
    assert(zsParseIndex(t, tl, (long long)zip_len, &idx) != 0);

    const char *bad_names[] = { "../evil", "a/../../evil", "/abs", "c:\\evil", "a\\b" };
    for (size_t i = 0; i < sizeof(bad_names) / sizeof(bad_names[0]); i++) {
        const Spec bad[] = { { "ok.bin", 10, 0, 0 }, { bad_names[i], 10, 0, 0 } };
        build_zip(bad, 2, 0);
        t = tail(64 * 1024, &tl);
        assert(zsParseIndex(t, tl, (long long)zip_len, &idx) != 0);
    }

    puts("zip_stream: all checks passed");
    return 0;
}
