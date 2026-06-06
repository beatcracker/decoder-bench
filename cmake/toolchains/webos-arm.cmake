cmake_path(SET _decoder_bench_toolchain_dir NORMALIZE "${CMAKE_CURRENT_LIST_DIR}")
cmake_path(GET _decoder_bench_toolchain_dir PARENT_PATH _decoder_bench_cmake_dir)
cmake_path(GET _decoder_bench_cmake_dir PARENT_PATH _decoder_bench_root)
set(_decoder_bench_sdk_installer "${_decoder_bench_root}/scripts/toolchain/install-webos-sdk.sh")

if (DEFINED ENV{DECODER_BENCH_WEBOS_NDK} AND NOT "$ENV{DECODER_BENCH_WEBOS_NDK}" STREQUAL "")
    cmake_path(SET _decoder_bench_webos_ndk NORMALIZE "$ENV{DECODER_BENCH_WEBOS_NDK}")
else ()
    execute_process(
            COMMAND "${_decoder_bench_sdk_installer}" --print-default-path
            OUTPUT_VARIABLE _decoder_bench_webos_ndk
            RESULT_VARIABLE _decoder_bench_sdk_path_result
            OUTPUT_STRIP_TRAILING_WHITESPACE)
    if (NOT _decoder_bench_sdk_path_result EQUAL 0)
        message(FATAL_ERROR "Could not resolve default webOS SDK path from ${_decoder_bench_sdk_installer}")
    endif ()
    cmake_path(SET _decoder_bench_webos_ndk NORMALIZE "${_decoder_bench_webos_ndk}")
endif ()

set(_decoder_bench_real_toolchain "${_decoder_bench_webos_ndk}/share/buildroot/toolchainfile.cmake")
if (NOT EXISTS "${_decoder_bench_real_toolchain}")
    message(FATAL_ERROR
            "webOS SDK is not installed at ${_decoder_bench_webos_ndk}. "
            "Run `mise run sdk-install`, or set DECODER_BENCH_WEBOS_NDK to the SDK root.")
endif ()

set(ENV{WEBOS_NDK} "${_decoder_bench_webos_ndk}")
set(WEBOS_NDK "${_decoder_bench_webos_ndk}" CACHE PATH "Resolved webOS SDK root")
set(DECODER_BENCH_TARGET_WEBOS ON CACHE BOOL "Build for webOS" FORCE)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(_decoder_bench_pkg_config "${_decoder_bench_root}/scripts/toolchain/use-webos-pkg-config.sh")
set(PKG_CONFIG_EXECUTABLE "${_decoder_bench_pkg_config}" CACHE FILEPATH "webOS target pkg-config wrapper" FORCE)

include("${_decoder_bench_real_toolchain}")

set(PKG_CONFIG_EXECUTABLE "${_decoder_bench_pkg_config}" CACHE FILEPATH "webOS target pkg-config wrapper" FORCE)
