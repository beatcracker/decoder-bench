#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "bench_source.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "bench_annexb.h"

#ifdef BENCH_SOURCE_TEST_HOOKS
#include "bench_source_internal.h"
#endif

/* ---------- Storage I/O serialization ---------- */

static pthread_mutex_t g_storage_io_lock = PTHREAD_MUTEX_INITIALIZER;

static ssize_t bench_storage_read(int fd, void *buf, size_t count) {
    size_t got = 0;
    while (got < count) {
        size_t want = count - got;
        if (want > BENCH_STORAGE_CHUNK) {
            want = BENCH_STORAGE_CHUNK;
        }
        pthread_mutex_lock(&g_storage_io_lock);
        ssize_t n = read(fd, (char *)buf + got, want);
        int saved_errno = errno;
        pthread_mutex_unlock(&g_storage_io_lock);
        if (n < 0) {
            if (saved_errno == EINTR) {
                continue;
            }
            errno = saved_errno;
            return -1;
        }
        if (n == 0) {
            break;
        }
        got += (size_t)n;
    }
    return (ssize_t)got;
}

static int64_t source_clock_now_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        fprintf(stderr, "bench_source: clock_gettime(CLOCK_MONOTONIC) failed: %s\n", strerror(errno));
        _exit(2);
    }
    return (int64_t)ts.tv_sec * 1000000000LL + (int64_t)ts.tv_nsec;
}

static int64_t source_clock_now_ms(void) {
    return source_clock_now_ns() / 1000000LL;
}

int bench_storage_warmup(const char *path, size_t limit_bytes, int max_ms) {
    if (path == NULL) {
        return -1;
    }
    if (limit_bytes == 0) {
        limit_bytes = BENCH_WARMUP_BYTES_DEFAULT;
    }
    if (max_ms <= 0) {
        max_ms = BENCH_WARMUP_MAX_MS_DEFAULT;
    }

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "bench_source: warmup cannot open '%s': %s\n", path, strerror(errno));
        return -1;
    }

    unsigned char *buf = malloc(BENCH_STORAGE_CHUNK);
    if (buf == NULL) {
        close(fd);
        return -1;
    }

    int64_t deadline = source_clock_now_ms() + max_ms;
    size_t total = 0;
    int rc = 0;
    while (total < limit_bytes) {
        if (source_clock_now_ms() > deadline) {
            rc = -1;
            break;
        }
        size_t want = limit_bytes - total;
        if (want > BENCH_STORAGE_CHUNK) {
            want = BENCH_STORAGE_CHUNK;
        }
        ssize_t n = bench_storage_read(fd, buf, want);
        if (n < 0) {
            rc = -1;
            break;
        }
        if (n == 0) {
            break;
        }
        total += (size_t)n;
    }

#ifdef POSIX_FADV_DONTNEED
    (void)posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
#endif

    free(buf);
    close(fd);
    return rc;
}

typedef struct BufferSlot {
    unsigned char *data;
    size_t size;
    size_t capacity;
    AccessUnit *aus;
    int au_count;
    int au_cap;
    int au_next_idx;
    bool eof_seen;
} BufferSlot;

struct BenchSource {
    int fd;

    BenchSourceMode mode;
    unsigned int source_buffer_mib;
    size_t buffer_capacity;

    SS4S_VideoCodec codec;
    int width;
    int height;

    BufferSlot buffers[2];
    int active_idx;

    unsigned char *carry_data;
    size_t carry_size;
    size_t carry_capacity;

    int target_frames;
    int frames_delivered;

    bool au_acquired;

    /* Streaming mode only */
    bool needs_promotion;
    pthread_t loader_thread;
    bool loader_thread_started;
    pthread_mutex_t state_lock;
    pthread_cond_t state_cv;
    bool loader_stop;
    bool inactive_needs_refill;
    bool inactive_loaded;
    bool inactive_eof;
    BenchSourceError loader_error;

    const volatile sig_atomic_t *interrupted;
};

struct BenchSourcePrefetch {
    pthread_t thread;
    bool ready;
    char fixture_path[BENCH_MAX_PATH_LEN];
    BenchSourceOptions options;
    const volatile sig_atomic_t *interrupted;
    BenchSourceOutcome outcome;
    BenchSource *source;
    pthread_mutex_t lock;
    pthread_cond_t cv;
};

static unsigned int clamp_buffer_mib(unsigned int requested) {
    unsigned int v = requested == 0 ? BENCH_SOURCE_BUFFER_DEFAULT_MIB : requested;
    if (v < BENCH_SOURCE_BUFFER_MIN_MIB) {
        v = BENCH_SOURCE_BUFFER_MIN_MIB;
    }
    if (v > BENCH_SOURCE_BUFFER_MAX_MIB) {
        v = BENCH_SOURCE_BUFFER_MAX_MIB;
    }
    return v;
}

static void slot_free(BufferSlot *slot) {
    if (slot == NULL) {
        return;
    }
    free(slot->data);
    free(slot->aus);
    memset(slot, 0, sizeof(*slot));
}

static int slot_alloc(BufferSlot *slot, size_t capacity) {
    if (slot == NULL || capacity == 0) {
        return -1;
    }
    slot->data = malloc(capacity);
    if (slot->data == NULL) {
        return -1;
    }
    slot->capacity = capacity;
    slot->size = 0;
    slot->aus = NULL;
    slot->au_count = 0;
    slot->au_cap = 0;
    slot->au_next_idx = 0;
    slot->eof_seen = false;
    return 0;
}

static BenchSourceError fill_buffer(BenchSource *source, BufferSlot *slot) {
    if (source == NULL || slot == NULL) {
        return BENCH_SOURCE_ERROR_IO;
    }
    if (source->carry_size > BENCH_MAX_SINGLE_AU_BYTES || source->carry_size > source->carry_capacity) {
        fprintf(stderr, "bench_source: carry %zu exceeds single-AU limit %u\n", source->carry_size,
                BENCH_MAX_SINGLE_AU_BYTES);
        return BENCH_SOURCE_ERROR_INVALID_FIXTURE;
    }
    if (source->carry_size > slot->capacity) {
        fprintf(stderr, "bench_source: carry %zu exceeds buffer capacity %zu\n", source->carry_size, slot->capacity);
        return BENCH_SOURCE_ERROR_INVALID_FIXTURE;
    }

    free(slot->aus);
    slot->aus = NULL;
    slot->au_count = 0;
    slot->au_cap = 0;
    slot->au_next_idx = 0;
    slot->size = 0;
    slot->eof_seen = false;

    if (source->carry_size > 0) {
        memcpy(slot->data, source->carry_data, source->carry_size);
    }

    size_t want = slot->capacity - source->carry_size;
    size_t got = 0;
    if (want > 0) {
        ssize_t n = bench_storage_read(source->fd, slot->data + source->carry_size, want);
        if (n < 0) {
            fprintf(stderr, "bench_source: read failed: %s\n", strerror(errno));
            return BENCH_SOURCE_ERROR_IO;
        }
        got = (size_t)n;
    }
    slot->size = source->carry_size + got;
    bool eof = got < want;
    slot->eof_seen = eof;

    if (slot->size == 0) {
        return BENCH_SOURCE_ERROR_NONE;
    }

    size_t carry_offset = slot->size;
    if (bench_annexb_parse_access_units(slot->data, slot->size, eof, &source->codec, &source->width, &source->height,
                                        &slot->aus, &slot->au_count, &slot->au_cap, &carry_offset) != 0) {
        return BENCH_SOURCE_ERROR_INVALID_FIXTURE;
    }

    if (!eof) {
        size_t new_carry = slot->size - carry_offset;
        if (new_carry > BENCH_MAX_SINGLE_AU_BYTES || new_carry > source->carry_capacity) {
            return BENCH_SOURCE_ERROR_INVALID_FIXTURE;
        }
        if (new_carry > 0) {
            memmove(source->carry_data, slot->data + carry_offset, new_carry);
        }
        source->carry_size = new_carry;
    } else {
        source->carry_size = 0;
    }

    return BENCH_SOURCE_ERROR_NONE;
}

static void *loader_main(void *arg) {
    BenchSource *source = arg;
    pthread_mutex_lock(&source->state_lock);
    while (!source->loader_stop) {
        if (source->inactive_eof) {
            source->inactive_needs_refill = false;
            pthread_cond_broadcast(&source->state_cv);
            pthread_cond_wait(&source->state_cv, &source->state_lock);
            continue;
        }
        if (!source->inactive_needs_refill) {
            pthread_cond_wait(&source->state_cv, &source->state_lock);
            continue;
        }
        if (source->loader_error != BENCH_SOURCE_ERROR_NONE) {
            source->inactive_needs_refill = false;
            pthread_cond_broadcast(&source->state_cv);
            pthread_cond_wait(&source->state_cv, &source->state_lock);
            continue;
        }

        int target_idx = 1 - source->active_idx;
        BufferSlot *slot = &source->buffers[target_idx];
        pthread_mutex_unlock(&source->state_lock);

        BenchSourceError err = fill_buffer(source, slot);

        pthread_mutex_lock(&source->state_lock);
        source->inactive_needs_refill = false;
        if (err != BENCH_SOURCE_ERROR_NONE) {
            source->loader_error = err;
        } else {
            source->inactive_loaded = true;
            if (slot->eof_seen) {
                source->inactive_eof = true;
            }
        }
        pthread_cond_broadcast(&source->state_cv);
    }
    pthread_mutex_unlock(&source->state_lock);
    return NULL;
}

static void source_destroy(BenchSource *source) {
    if (source == NULL) {
        return;
    }
    if (source->au_acquired) {
        fprintf(stderr, "bench_source: closing source with an acquired access unit\n");
    }
    if (source->loader_thread_started) {
        pthread_mutex_lock(&source->state_lock);
        source->loader_stop = true;
        pthread_cond_broadcast(&source->state_cv);
        pthread_mutex_unlock(&source->state_lock);
        pthread_join(source->loader_thread, NULL);
        pthread_cond_destroy(&source->state_cv);
        pthread_mutex_destroy(&source->state_lock);
    }
    slot_free(&source->buffers[0]);
    slot_free(&source->buffers[1]);
    free(source->carry_data);
    if (source->fd >= 0) {
        close(source->fd);
    }
    free(source);
}

static BenchSourceOutcome promote_to_streaming(BenchSource *source) {
    bool mutex_initialized = false;
    bool cond_initialized = false;

    if (source == NULL) {
        return BENCH_SOURCE_IO_ERROR;
    }
    if (source->interrupted != NULL && *source->interrupted != 0) {
        return BENCH_SOURCE_STOPPED;
    }
    if (source->mode != BENCH_SOURCE_MODE_STREAMING || !source->needs_promotion) {
        return BENCH_SOURCE_OK;
    }

    if (slot_alloc(&source->buffers[1], source->buffer_capacity) != 0) {
        return BENCH_SOURCE_IO_ERROR;
    }
    if (pthread_mutex_init(&source->state_lock, NULL) != 0) {
        goto fail;
    }
    mutex_initialized = true;
    if (pthread_cond_init(&source->state_cv, NULL) != 0) {
        goto fail;
    }
    cond_initialized = true;

    source->inactive_needs_refill = true;
    source->inactive_loaded = false;
    source->inactive_eof = false;
    source->loader_error = BENCH_SOURCE_ERROR_NONE;
    source->loader_stop = false;
    if (pthread_create(&source->loader_thread, NULL, loader_main, source) != 0) {
        goto fail;
    }

    source->loader_thread_started = true;
    source->needs_promotion = false;
    return BENCH_SOURCE_OK;

fail:
    if (cond_initialized) {
        pthread_cond_destroy(&source->state_cv);
    }
    if (mutex_initialized) {
        pthread_mutex_destroy(&source->state_lock);
    }
    slot_free(&source->buffers[1]);
    source->inactive_needs_refill = false;
    source->inactive_loaded = false;
    source->inactive_eof = false;
    source->loader_error = BENCH_SOURCE_ERROR_NONE;
    source->loader_stop = false;
    return BENCH_SOURCE_IO_ERROR;
}

BenchSourceOutcome bench_source_open(const char *path, const BenchSourceOptions *options,
                                     const volatile sig_atomic_t *interrupted, BenchSource **out_source) {
    if (path == NULL || out_source == NULL) {
        return BENCH_SOURCE_IO_ERROR;
    }
    *out_source = NULL;

    if (interrupted != NULL && *interrupted != 0) {
        return BENCH_SOURCE_STOPPED;
    }

    unsigned int buf_mib = clamp_buffer_mib(options != NULL ? options->source_buffer_mib : 0);
    int target_frames = options != NULL ? options->target_frames : 0;

    BenchSource *source = calloc(1, sizeof(*source));
    if (source == NULL) {
        return BENCH_SOURCE_IO_ERROR;
    }
    source->fd = -1;
    source->codec = SS4S_VIDEO_NONE;
    source->source_buffer_mib = buf_mib;
    source->buffer_capacity = (size_t)buf_mib * 1024u * 1024u;
    source->carry_capacity = BENCH_MAX_SINGLE_AU_BYTES;
    source->target_frames = target_frames;
    source->interrupted = interrupted;
    source->active_idx = 0;

    source->fd = open(path, O_RDONLY);
    if (source->fd < 0) {
        fprintf(stderr, "bench_source: cannot open '%s': %s\n", path, strerror(errno));
        source_destroy(source);
        return BENCH_SOURCE_IO_ERROR;
    }
    struct stat st;
    if (fstat(source->fd, &st) != 0 || st.st_size <= 0) {
        fprintf(stderr, "bench_source: cannot stat '%s'\n", path);
        source_destroy(source);
        return BENCH_SOURCE_IO_ERROR;
    }

    if (slot_alloc(&source->buffers[0], source->buffer_capacity) != 0) {
        source_destroy(source);
        return BENCH_SOURCE_IO_ERROR;
    }
    source->carry_data = malloc(source->carry_capacity);
    if (source->carry_data == NULL) {
        source_destroy(source);
        return BENCH_SOURCE_IO_ERROR;
    }

    BenchSourceError err = fill_buffer(source, &source->buffers[0]);
    if (err != BENCH_SOURCE_ERROR_NONE) {
        source_destroy(source);
        if (err == BENCH_SOURCE_ERROR_INVALID_FIXTURE) {
            return BENCH_SOURCE_INVALID_FIXTURE;
        }
        return BENCH_SOURCE_IO_ERROR;
    }

    if (source->buffers[0].au_count == 0) {
        fprintf(stderr, "bench_source: no complete access units in first buffer\n");
        source_destroy(source);
        return BENCH_SOURCE_INVALID_FIXTURE;
    }
    if (!source->buffers[0].aus[0].is_keyframe) {
        fprintf(stderr, "bench_source: first access unit is not a random-access frame\n");
        source_destroy(source);
        return BENCH_SOURCE_INVALID_FIXTURE;
    }
    if (source->codec == SS4S_VIDEO_NONE || source->width <= 0 || source->height <= 0) {
        fprintf(stderr, "bench_source: parameter set or dimensions not detected\n");
        source_destroy(source);
        return BENCH_SOURCE_INVALID_FIXTURE;
    }

    if (source->buffers[0].eof_seen) {
        source->mode = BENCH_SOURCE_MODE_COMPLETE;
    } else {
        source->mode = BENCH_SOURCE_MODE_STREAMING;
        source->needs_promotion = true;
    }

    *out_source = source;
    return BENCH_SOURCE_OK;
}

void bench_source_close(BenchSource *source) {
    source_destroy(source);
}

BenchSourceOutcome bench_source_prepare_playback(BenchSource *source) {
    return promote_to_streaming(source);
}

SS4S_VideoCodec bench_source_codec(const BenchSource *source) {
    return source != NULL ? source->codec : SS4S_VIDEO_NONE;
}

void bench_source_dimensions(const BenchSource *source, int *width, int *height) {
    if (source == NULL) {
        if (width != NULL) {
            *width = 0;
        }
        if (height != NULL) {
            *height = 0;
        }
        return;
    }
    if (width != NULL) {
        *width = source->width;
    }
    if (height != NULL) {
        *height = source->height;
    }
}

BenchSourceMode bench_source_mode(const BenchSource *source) {
    return source != NULL ? source->mode : BENCH_SOURCE_MODE_NONE;
}

unsigned int bench_source_buffer_mib(const BenchSource *source) {
    return source != NULL ? source->source_buffer_mib : 0;
}

static BenchSourceOutcome streaming_swap(BenchSource *source) {
    pthread_mutex_lock(&source->state_lock);

    if (source->interrupted != NULL && *source->interrupted != 0) {
        pthread_mutex_unlock(&source->state_lock);
        return BENCH_SOURCE_STOPPED;
    }

    if (source->loader_error != BENCH_SOURCE_ERROR_NONE) {
        BenchSourceError err = source->loader_error;
        pthread_mutex_unlock(&source->state_lock);
        if (err == BENCH_SOURCE_ERROR_INVALID_FIXTURE) {
            return BENCH_SOURCE_INVALID_FIXTURE;
        }
        if (err == BENCH_SOURCE_ERROR_STORAGE_UNDERFLOW) {
            return BENCH_SOURCE_STORAGE_UNDERFLOW;
        }
        return BENCH_SOURCE_IO_ERROR;
    }
    if (!source->inactive_loaded) {
        pthread_mutex_unlock(&source->state_lock);
        return BENCH_SOURCE_STORAGE_UNDERFLOW;
    }

    int old_active = source->active_idx;
    int new_active = 1 - old_active;
    source->active_idx = new_active;
    source->inactive_loaded = false;

    BufferSlot *old_slot = &source->buffers[old_active];
    old_slot->au_count = 0;
    old_slot->au_next_idx = 0;

    bool new_active_was_eof = source->buffers[new_active].eof_seen;
    if (!new_active_was_eof && !source->inactive_eof) {
        source->inactive_needs_refill = true;
        pthread_cond_broadcast(&source->state_cv);
    }
    pthread_mutex_unlock(&source->state_lock);

    return BENCH_SOURCE_OK;
}

BenchSourceOutcome bench_source_acquire(BenchSource *source, AccessUnit *au_out) {
    if (source == NULL || au_out == NULL) {
        return BENCH_SOURCE_IO_ERROR;
    }
    if (source->au_acquired) {
        return BENCH_SOURCE_IO_ERROR;
    }
    if (source->interrupted != NULL && *source->interrupted != 0) {
        return BENCH_SOURCE_STOPPED;
    }
    if (source->mode == BENCH_SOURCE_MODE_STREAMING && source->needs_promotion) {
        return BENCH_SOURCE_IO_ERROR;
    }

    BufferSlot *active = &source->buffers[source->active_idx];
    if (active->au_next_idx >= active->au_count) {
        if (active->eof_seen) {
            if (source->target_frames > 0 && source->frames_delivered < source->target_frames) {
                return BENCH_SOURCE_INVALID_FIXTURE;
            }
            return BENCH_SOURCE_EOF;
        }
        if (source->mode != BENCH_SOURCE_MODE_STREAMING) {
            return BENCH_SOURCE_IO_ERROR;
        }
        BenchSourceOutcome rc = streaming_swap(source);
        if (rc != BENCH_SOURCE_OK) {
            return rc;
        }
        active = &source->buffers[source->active_idx];
        if (active->au_count == 0) {
            if (active->eof_seen) {
                if (source->target_frames > 0 && source->frames_delivered < source->target_frames) {
                    return BENCH_SOURCE_INVALID_FIXTURE;
                }
                return BENCH_SOURCE_EOF;
            }
            return BENCH_SOURCE_INVALID_FIXTURE;
        }
    }

    *au_out = active->aus[active->au_next_idx];
    active->au_next_idx++;
    source->frames_delivered++;
    source->au_acquired = true;
    return BENCH_SOURCE_OK;
}

BenchSourceOutcome bench_source_release(BenchSource *source) {
    if (source == NULL || !source->au_acquired) {
        return BENCH_SOURCE_IO_ERROR;
    }
    source->au_acquired = false;
    return BENCH_SOURCE_OK;
}

static void *prefetch_thread_main(void *arg) {
    BenchSourcePrefetch *prefetch = arg;
    BenchSource *source = NULL;
    BenchSourceOutcome rc =
        bench_source_open(prefetch->fixture_path, &prefetch->options, prefetch->interrupted, &source);
    pthread_mutex_lock(&prefetch->lock);
    prefetch->outcome = rc;
    prefetch->source = source;
    prefetch->ready = true;
    pthread_cond_broadcast(&prefetch->cv);
    pthread_mutex_unlock(&prefetch->lock);
    return NULL;
}

static void prefetch_destroy(BenchSourcePrefetch *prefetch) {
    if (prefetch == NULL) {
        return;
    }
    pthread_cond_destroy(&prefetch->cv);
    pthread_mutex_destroy(&prefetch->lock);
    free(prefetch);
}

BenchSourceOutcome bench_source_prefetch_start(const char *path, const BenchSourceOptions *options,
                                               const volatile sig_atomic_t *interrupted,
                                               BenchSourcePrefetch **out_prefetch) {
    if (path == NULL || out_prefetch == NULL) {
        return BENCH_SOURCE_IO_ERROR;
    }
    *out_prefetch = NULL;

    BenchSourcePrefetch *prefetch = calloc(1, sizeof(*prefetch));
    if (prefetch == NULL) {
        return BENCH_SOURCE_IO_ERROR;
    }

    int written = snprintf(prefetch->fixture_path, sizeof(prefetch->fixture_path), "%s", path);
    if (written < 0 || (size_t)written >= sizeof(prefetch->fixture_path)) {
        free(prefetch);
        return BENCH_SOURCE_IO_ERROR;
    }
    if (options != NULL) {
        prefetch->options = *options;
    }
    prefetch->interrupted = interrupted;
    prefetch->outcome = BENCH_SOURCE_IO_ERROR;

    if (pthread_mutex_init(&prefetch->lock, NULL) != 0) {
        free(prefetch);
        return BENCH_SOURCE_IO_ERROR;
    }
    if (pthread_cond_init(&prefetch->cv, NULL) != 0) {
        pthread_mutex_destroy(&prefetch->lock);
        free(prefetch);
        return BENCH_SOURCE_IO_ERROR;
    }
    if (pthread_create(&prefetch->thread, NULL, prefetch_thread_main, prefetch) != 0) {
        prefetch_destroy(prefetch);
        return BENCH_SOURCE_IO_ERROR;
    }

    *out_prefetch = prefetch;
    return BENCH_SOURCE_OK;
}

BenchSourceOutcome bench_source_prefetch_join(BenchSourcePrefetch *prefetch, BenchSource **out_source) {
    if (out_source != NULL) {
        *out_source = NULL;
    }
    if (prefetch == NULL || out_source == NULL) {
        return BENCH_SOURCE_IO_ERROR;
    }

    pthread_mutex_lock(&prefetch->lock);
    while (!prefetch->ready) {
        pthread_cond_wait(&prefetch->cv, &prefetch->lock);
    }
    BenchSourceOutcome rc = prefetch->outcome;
    *out_source = prefetch->source;
    prefetch->source = NULL;
    pthread_mutex_unlock(&prefetch->lock);

    pthread_join(prefetch->thread, NULL);
    prefetch_destroy(prefetch);
    return rc;
}

void bench_source_prefetch_abandon(BenchSourcePrefetch *prefetch) {
    if (prefetch == NULL) {
        return;
    }
    BenchSource *source = NULL;
    (void)bench_source_prefetch_join(prefetch, &source);
    if (source != NULL) {
        bench_source_close(source);
    }
}

BenchSourceError bench_source_outcome_to_error(BenchSourceOutcome outcome) {
    switch (outcome) {
    case BENCH_SOURCE_OK:
    case BENCH_SOURCE_EOF:
    case BENCH_SOURCE_STOPPED:
        return BENCH_SOURCE_ERROR_NONE;
    case BENCH_SOURCE_STORAGE_UNDERFLOW:
        return BENCH_SOURCE_ERROR_STORAGE_UNDERFLOW;
    case BENCH_SOURCE_INVALID_FIXTURE:
        return BENCH_SOURCE_ERROR_INVALID_FIXTURE;
    case BENCH_SOURCE_IO_ERROR:
    default:
        return BENCH_SOURCE_ERROR_IO;
    }
}

const char *bench_source_error_str(BenchSourceError err) {
    switch (err) {
    case BENCH_SOURCE_ERROR_NONE:
        return "none";
    case BENCH_SOURCE_ERROR_STORAGE_UNDERFLOW:
        return "storage-underflow";
    case BENCH_SOURCE_ERROR_INVALID_FIXTURE:
        return "invalid-fixture";
    case BENCH_SOURCE_ERROR_IO:
        return "io-error";
    default:
        return "unknown";
    }
}

const char *bench_source_mode_str(BenchSourceMode mode) {
    switch (mode) {
    case BENCH_SOURCE_MODE_NONE:
        return "none";
    case BENCH_SOURCE_MODE_COMPLETE:
        return "complete";
    case BENCH_SOURCE_MODE_STREAMING:
        return "streaming";
    default:
        return "unknown";
    }
}

#ifdef BENCH_SOURCE_TEST_HOOKS
bool bench_source_needs_promotion_for_testing(const BenchSource *source) {
    return source != NULL && source->needs_promotion;
}

bool bench_source_loader_started_for_testing(const BenchSource *source) {
    return source != NULL && source->loader_thread_started;
}

bool bench_source_buffer_allocated_for_testing(const BenchSource *source, int index) {
    return source != NULL && index >= 0 && index < 2 && source->buffers[index].data != NULL;
}

bool bench_source_has_acquired_for_testing(const BenchSource *source) {
    return source != NULL && source->au_acquired;
}

int bench_source_active_buffer_for_testing(const BenchSource *source) {
    return source != NULL ? source->active_idx : -1;
}

int bench_source_active_au_remaining_for_testing(const BenchSource *source) {
    if (source == NULL || source->active_idx < 0 || source->active_idx > 1) {
        return -1;
    }
    const BufferSlot *slot = &source->buffers[source->active_idx];
    if (slot->au_next_idx >= slot->au_count) {
        return 0;
    }
    return slot->au_count - slot->au_next_idx;
}

bool bench_source_wait_inactive_loaded_for_testing(BenchSource *source, int timeout_ms) {
    if (source == NULL || !source->loader_thread_started || timeout_ms < 0) {
        return false;
    }

    struct timespec deadline;
    if (clock_gettime(CLOCK_REALTIME, &deadline) != 0) {
        return false;
    }
    deadline.tv_sec += timeout_ms / 1000;
    long extra_ns = (long)(timeout_ms % 1000) * 1000000L;
    if (deadline.tv_nsec > 999999999L - extra_ns) {
        deadline.tv_sec++;
        deadline.tv_nsec = deadline.tv_nsec - (1000000000L - extra_ns);
    } else {
        deadline.tv_nsec += extra_ns;
    }

    pthread_mutex_lock(&source->state_lock);
    while (!source->inactive_loaded && source->loader_error == BENCH_SOURCE_ERROR_NONE) {
        int rc = pthread_cond_timedwait(&source->state_cv, &source->state_lock, &deadline);
        if (rc == ETIMEDOUT) {
            break;
        }
    }
    bool ready = source->inactive_loaded;
    pthread_mutex_unlock(&source->state_lock);
    return ready;
}

size_t bench_source_carry_capacity_for_testing(const BenchSource *source) {
    return source != NULL ? source->carry_capacity : 0;
}
#endif
