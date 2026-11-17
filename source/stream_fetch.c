#include "stream_fetch.h"
#include "zip_stream.h"
#include "config.h"
#include "applog.h"
#include <switch.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* One connection per bsd/ssl session: libnx gives the app 3 of each, and a
   transfer holds one while it waits on the socket. */
#define STREAM_CONNECTIONS  3
#define STREAM_TAIL_BYTES   (256 * 1024)
#define STREAM_BATCH_BYTES  (16LL * 1024 * 1024)
#define STREAM_FILE_BUF     (1024 * 1024)

typedef struct {
    int first, last;   /* entry range fetched with one request */
} Batch;

typedef struct {
    const ZsIndex *idx;
    const char    *url;         /* as published; redirects to a signed address */
    const char    *direct_url;  /* that signed address, good for about an hour */
    const char    *dest;
    Batch         *batches;
    int            batch_count;

    pthread_mutex_t lock;
    int            next_batch;
    int            workers;
    long long      done_bytes;
    long long      total_bytes;
    bool           failed;
    char           reason[160];

    DownloadProgressCb cb;
    void          *userdata;
} Job;

typedef struct {
    Job      *job;
    CURL     *curl;
    ZsSink    sink;
    long long batch_bytes;   /* counted toward progress in this attempt */
    bool      checked_status;

    /* where the time went, for the log */
    int       id;
    long long bytes, chunks, requests, retries;
    long long io_ns, crc_ns;
    double    connect_secs;  /* TCP + TLS setup of the first request */
} Worker;

static void add_progress(Job *job, long long delta) {
    pthread_mutex_lock(&job->lock);
    job->done_bytes += delta;
    long long done = job->done_bytes;
    pthread_mutex_unlock(&job->lock);
    if (job->cb) job->cb((size_t)done, (size_t)job->total_bytes, job->userdata);
}

static bool job_failed(Job *job) {
    pthread_mutex_lock(&job->lock);
    bool f = job->failed;
    pthread_mutex_unlock(&job->lock);
    return f;
}

static void job_fail(Job *job, const char *reason) {
    pthread_mutex_lock(&job->lock);
    if (!job->failed) {
        job->failed = true;
        snprintf(job->reason, sizeof(job->reason), "%s", reason);
    }
    pthread_mutex_unlock(&job->lock);
}

static int take_batch(Job *job) {
    pthread_mutex_lock(&job->lock);
    int b = (!job->failed && job->next_batch < job->batch_count) ? job->next_batch++ : -1;
    pthread_mutex_unlock(&job->lock);
    return b;
}

static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *ctx) {
    Worker *w = ctx;
    size_t bytes = size * nmemb;

    if (job_failed(w->job)) return 0;

    if (!w->checked_status) {
        /* anything but 206 is an error page or the whole file from byte 0 */
        long code = 0;
        curl_easy_getinfo(w->curl, CURLINFO_RESPONSE_CODE, &code);
        if (code != 206) {
            snprintf(w->sink.err, sizeof(w->sink.err), "server returned HTTP %ld", code);
            return 0;
        }
        w->checked_status = true;
    }

    if (zsSinkFeed(&w->sink, ptr, bytes) != 0) return 0;

    w->batch_bytes += (long long)bytes;
    w->chunks++;
    add_progress(w->job, (long long)bytes);
    return bytes;
}

static bool fetch_batch(Worker *w, const Batch *b, char *iobuf,
                        char *why, size_t why_size) {
    Job *job = w->job;
    const ZsEntry *first = &job->idx->entries[b->first];
    const ZsEntry *last  = &job->idx->entries[b->last];

    char range[64];
    snprintf(range, sizeof(range), "%lld-%lld", first->hdr_off, last->end_off - 1);

    for (int attempt = 1; attempt <= DOWNLOAD_MAX_ATTEMPTS; attempt++) {
        zsSinkBegin(&w->sink, job->idx, job->dest, b->first, b->last, iobuf, STREAM_FILE_BUF);
        w->batch_bytes = 0;
        w->checked_status = false;

        /* the signed address can expire or be refused; go back through the
           published one after a failure */
        curl_easy_setopt(w->curl, CURLOPT_URL, attempt == 1 ? job->direct_url : job->url);
        curl_easy_setopt(w->curl, CURLOPT_RANGE, range);

        CURLcode res = curl_easy_perform(w->curl);
        w->io_ns += w->sink.io_ns;
        w->crc_ns += w->sink.crc_ns;
        w->requests++;
        if (w->requests == 1)
            curl_easy_getinfo(w->curl, CURLINFO_APPCONNECT_TIME, &w->connect_secs);
        if (res == CURLE_OK && zsSinkFinish(&w->sink) == 0) {
            w->bytes += w->batch_bytes;
            return true;
        }
        w->retries++;

        snprintf(why, why_size, "%s",
                 w->sink.err[0] ? w->sink.err : curl_easy_strerror(res));
        zsSinkAbort(&w->sink);
        add_progress(job, -w->batch_bytes);
        if (job_failed(job)) return false;
        appLog("stream: bytes %s attempt %d/%d failed: %s",
               range, attempt, DOWNLOAD_MAX_ATTEMPTS, why);
    }
    return false;
}

static void *worker_main(void *arg) {
    Job *job = arg;
    Worker w = { .job = job };
    u64 t0 = armGetSystemTick();

    pthread_mutex_lock(&job->lock);
    w.id = ++job->workers;
    pthread_mutex_unlock(&job->lock);

    char *iobuf = malloc(STREAM_FILE_BUF);
    w.curl = downloadMakeCurl(job->direct_url);
    if (!w.curl || !iobuf) {
        job_fail(job, "out of memory");
    } else {
        curl_easy_setopt(w.curl, CURLOPT_WRITEFUNCTION, write_cb);
        curl_easy_setopt(w.curl, CURLOPT_WRITEDATA, &w);

        int b;
        char why[160];
        while ((b = take_batch(job)) >= 0) {
            if (!fetch_batch(&w, &job->batches[b], iobuf, why, sizeof(why))) {
                job_fail(job, why);
                break;
            }
        }
    }

    if (w.curl) curl_easy_cleanup(w.curl);
    free(iobuf);

    /* total = SD card + checksum + everything else, and everything else is
       waiting on (and decrypting) the network */
    double total = (double)armTicksToNs(armGetSystemTick() - t0) / 1e9;
    double sd = (double)w.io_ns / 1e9, crc = (double)w.crc_ns / 1e9;
    appLog("stream: conn %d: %.1f MB in %lld requests (%lld retried), %.1f s = "
           "SD card %.1f s + checksum %.1f s + network %.1f s; "
           "connect %.2f s, avg read %lld bytes",
           w.id, (double)w.bytes / 1e6, w.requests, w.retries, total,
           sd, crc, total - sd - crc, w.connect_secs,
           w.chunks ? w.bytes / w.chunks : 0);
    return NULL;
}

/* group neighboring entries so one request carries about STREAM_BATCH_BYTES */
static int plan_batches(const ZsIndex *idx, Batch *out) {
    int n = 0;
    for (int i = 0; i < idx->count; ) {
        int j = i;
        while (j + 1 < idx->count &&
               idx->entries[j + 1].end_off - idx->entries[i].hdr_off <= STREAM_BATCH_BYTES)
            j++;
        out[n++] = (Batch){ .first = i, .last = j };
        i = j + 1;
    }
    return n;
}

int streamFetchZip(const char *url, long long zip_size, const char *dest_dir,
                   DownloadProgressCb cb, void *userdata,
                   char *reason, size_t reason_size) {
    snprintf(reason, reason_size, "unknown");
    if (zip_size <= 0) {
        snprintf(reason, reason_size, "package size unknown");
        return -1;
    }

    /* the zip's file list lives at its end; one small request gets it and
       tells us where the download really comes from */
    long long tail_len = zip_size < STREAM_TAIL_BYTES ? zip_size : STREAM_TAIL_BYTES;
    char range[48];
    snprintf(range, sizeof(range), "%lld-%lld", zip_size - tail_len, zip_size - 1);

    char direct_url[2048];
    char *tail = NULL;
    size_t got = 0;
    u64 t0 = armGetSystemTick();
    if (downloadRangeToMemory(url, range, &tail, &got, direct_url, sizeof(direct_url)) != 0) {
        snprintf(reason, reason_size, "server does not allow partial downloads");
        return -1;
    }
    appLog("stream: file list fetched in %.2f s",
           (double)armTicksToNs(armGetSystemTick() - t0) / 1e9);

    ZsIndex idx;
    int parsed = (long long)got == tail_len
               ? zsParseIndex((const unsigned char *)tail, got, zip_size, &idx) : -1;
    free(tail);
    if (parsed != 0) {
        snprintf(reason, reason_size, "package layout can't be streamed");
        return -1;
    }

    Job job = {
        .idx = &idx, .url = url, .direct_url = direct_url, .dest = dest_dir,
        .total_bytes = idx.cd_off - idx.entries[0].hdr_off,
        .cb = cb, .userdata = userdata,
    };
    job.batches = malloc(sizeof(Batch) * (size_t)idx.count);
    if (!job.batches) {
        zsFreeIndex(&idx);
        snprintf(reason, reason_size, "out of memory");
        return -1;
    }
    job.batch_count = plan_batches(&idx, job.batches);
    pthread_mutex_init(&job.lock, NULL);

    appLog("stream: %d files, %lld bytes, %d requests over %d connections",
           idx.count, job.total_bytes, job.batch_count, STREAM_CONNECTIONS);

    pthread_t threads[STREAM_CONNECTIONS];
    int started = 0;
    for (int i = 0; i < STREAM_CONNECTIONS; i++) {
        if (pthread_create(&threads[started], NULL, worker_main, &job) == 0)
            started++;
    }
    if (started == 0)
        worker_main(&job);   /* no threads to be had: one connection, right here */
    for (int i = 0; i < started; i++)
        pthread_join(threads[i], NULL);

    bool ok = !job.failed && job.next_batch == job.batch_count;
    if (!ok)
        snprintf(reason, reason_size, "%s", job.failed ? job.reason : "download incomplete");

    pthread_mutex_destroy(&job.lock);
    free(job.batches);
    zsFreeIndex(&idx);

    /* fsdev never flushes on its own; a reboot would drop what's cached */
    Result rc = fsdevCommitDevice("sdmc");
    if (R_FAILED(rc))
        appLog("WARNING: sdmc commit after stream download failed (rc=0x%X)", rc);
    return ok ? 0 : -1;
}
