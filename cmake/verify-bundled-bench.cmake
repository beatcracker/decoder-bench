if (NOT DEFINED DECODER_BENCH_BUNDLED_BENCH_DIR OR DECODER_BENCH_BUNDLED_BENCH_DIR STREQUAL "")
    message(FATAL_ERROR "DECODER_BENCH_BUNDLED_BENCH_DIR is not set")
endif ()

set(_decoder_bench_suites_dir "${DECODER_BENCH_BUNDLED_BENCH_DIR}/suites")
set(_decoder_bench_samples_dir "${DECODER_BENCH_BUNDLED_BENCH_DIR}/samples")

if (NOT IS_DIRECTORY "${_decoder_bench_suites_dir}")
    message(FATAL_ERROR
            "Bundled suite cache is missing at ${_decoder_bench_suites_dir}. Populate ${DECODER_BENCH_BUNDLED_BENCH_DIR} with prebuilt fixtures or run 'mise run bundled-gen' first.")
endif ()

if (NOT IS_DIRECTORY "${_decoder_bench_samples_dir}")
    message(FATAL_ERROR
            "Bundled sample cache is missing at ${_decoder_bench_samples_dir}. Populate ${DECODER_BENCH_BUNDLED_BENCH_DIR} with prebuilt fixtures or run 'mise run bundled-gen' first.")
endif ()

file(GLOB _decoder_bench_suite_files RELATIVE "${_decoder_bench_suites_dir}" "${_decoder_bench_suites_dir}/*.bench")
if (_decoder_bench_suite_files STREQUAL "")
    message(FATAL_ERROR
            "Bundled suite cache at ${_decoder_bench_suites_dir} is empty. Populate ${DECODER_BENCH_BUNDLED_BENCH_DIR} with prebuilt fixtures or run 'mise run bundled-gen' first.")
endif ()

file(GLOB _decoder_bench_sample_files RELATIVE "${_decoder_bench_samples_dir}" "${_decoder_bench_samples_dir}/*")
if (_decoder_bench_sample_files STREQUAL "")
    message(FATAL_ERROR
            "Bundled sample cache at ${_decoder_bench_samples_dir} is empty. Populate ${DECODER_BENCH_BUNDLED_BENCH_DIR} with prebuilt fixtures or run 'mise run bundled-gen' first.")
endif ()
