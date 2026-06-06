#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>

#include <NDL_directmedia_v2.h>

/*
 * Starfish media callback events, mirrored locally to avoid depending on the
 * SMP wrapper headers from this NDL-only compatibility shim.
 */
#define BENCH_NDL_EVENT_INT_ERROR 0x12
#define BENCH_NDL_EVENT_STR_ERROR 0x13

typedef struct BenchNdlLoadState {
    NDLMediaLoadCallback original_callback;
    bool load_failed;
    bool callback_diagnostic_logged;
    bool play_diagnostic_logged;
    int failed_type;
    long long failed_value;
} BenchNdlLoadState;

static pthread_mutex_t g_load_state_lock = PTHREAD_MUTEX_INITIALIZER;
static BenchNdlLoadState g_load_state;

int __real_NDL_DirectMediaLoad(NDL_DIRECTMEDIA_DATA_INFO_T *data, NDLMediaLoadCallback callback);
int __real_NDL_DirectVideoPlay(void *buffer, unsigned int size, long long pts);

static void bench_ndl_load_callback(int type, long long num_value, const char *str_value) {
    NDLMediaLoadCallback original_callback = NULL;
    bool log_failure = false;

    pthread_mutex_lock(&g_load_state_lock);
    original_callback = g_load_state.original_callback;
    if (type == BENCH_NDL_EVENT_INT_ERROR || type == BENCH_NDL_EVENT_STR_ERROR) {
        g_load_state.load_failed = true;
        g_load_state.failed_type = type;
        g_load_state.failed_value = num_value;
        if (!g_load_state.callback_diagnostic_logged) {
            g_load_state.callback_diagnostic_logged = true;
            log_failure = true;
        }
    }
    pthread_mutex_unlock(&g_load_state_lock);

    if (log_failure) {
        fprintf(stderr, "bench_ndl_webos_detect: NDL load callback error type=0x%02x numValue=0x%llx strValue=%p\n",
                type, num_value, (const void *)str_value);
    }

    if (original_callback != NULL) {
        original_callback(type, num_value, str_value);
    }
}

int __wrap_NDL_DirectMediaLoad(NDL_DIRECTMEDIA_DATA_INFO_T *data, NDLMediaLoadCallback callback) {
    pthread_mutex_lock(&g_load_state_lock);
    g_load_state = (BenchNdlLoadState){
        .original_callback = callback,
    };
    pthread_mutex_unlock(&g_load_state_lock);

    return __real_NDL_DirectMediaLoad(data, bench_ndl_load_callback);
}

int __wrap_NDL_DirectVideoPlay(void *buffer, unsigned int size, long long pts) {
    bool load_failed = false;
    bool log_failure = false;
    int failed_type = 0;
    long long failed_value = 0;

    pthread_mutex_lock(&g_load_state_lock);
    load_failed = g_load_state.load_failed;
    if (load_failed) {
        failed_type = g_load_state.failed_type;
        failed_value = g_load_state.failed_value;
        if (!g_load_state.play_diagnostic_logged) {
            g_load_state.play_diagnostic_logged = true;
            log_failure = true;
        }
    }
    pthread_mutex_unlock(&g_load_state_lock);

    if (load_failed) {
        if (log_failure) {
            fprintf(stderr,
                    "bench_ndl_webos_detect: blocking NDL_DirectVideoPlay after load error "
                    "type=0x%02x numValue=0x%llx\n",
                    failed_type, failed_value);
        }
        return -1;
    }

    return __real_NDL_DirectVideoPlay(buffer, size, pts);
}
