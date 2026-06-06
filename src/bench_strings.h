#pragma once

#include "config.h"

#define BENCH_STRINGS_CLI_HELP "--help"
#define BENCH_STRINGS_CLI_HELP_SHORT "-h"
#define BENCH_STRINGS_CLI_LIST "--list"
#define BENCH_STRINGS_CLI_SUITE "--suite"
#define BENCH_STRINGS_CLI_FILE "--file"
#define BENCH_STRINGS_CLI_FPS "--fps"
#define BENCH_STRINGS_CLI_RUN_SECONDS "--run-seconds"
#define BENCH_STRINGS_CLI_SOURCE_BUFFER_MIB "--source-buffer-mib"
#define BENCH_STRINGS_CLI_DIR "--dir"
#define BENCH_STRINGS_CLI_RESULTS_DIR "--results-dir"
#define BENCH_STRINGS_CLI_LOOP "--loop"

#define BENCH_STRINGS_ENV_APPID "APPID"
#define BENCH_STRINGS_ENV_BENCH_PATH "BENCH_PATH"
#define BENCH_STRINGS_PROC_SELF_EXE "/proc/self/exe"

#define BENCH_STRINGS_TMP_ROOT "/tmp/" DECODER_BENCH_APP_ID
#define BENCH_STRINGS_WEBOS_USB_ROOT BENCH_STRINGS_TMP_ROOT "/usb"
#define BENCH_STRINGS_WEBOS_PLATFORM_USB_ROOT "/tmp/usb"
#define BENCH_STRINGS_WEBOS_RUN_LOCK ".bench-usb.lock"

#define BENCH_STRINGS_DIR_BENCH "bench"
#define BENCH_STRINGS_DIR_SUITES "suites"
#define BENCH_STRINGS_DIR_SAMPLES "samples"
#define BENCH_STRINGS_DIR_RESULTS "results"
#define BENCH_STRINGS_DIR_USB_RESULTS "bench-results"

#define BENCH_STRINGS_LOG_STDOUT "bench.log"
#define BENCH_STRINGS_LOG_STDERR "bench.err"
#define BENCH_STRINGS_DIRECT_SCOPE "direct"
#define BENCH_STRINGS_SUITE_EXT ".bench"
#define BENCH_STRINGS_WRITE_TEST_FILE ".decoder-bench-write-test"
