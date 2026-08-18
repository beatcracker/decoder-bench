cmake_minimum_required(VERSION 4.3)

project(decoder-bench VERSION 1.0.0 LANGUAGES C)

include(CheckIncludeFile)
include(CheckStructHasMember)
include(CheckSymbolExists)
include(CTest)
include(GNUInstallDirs)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(DECODER_BENCH_APP_ID "io.github.beatcracker.decoderbench" CACHE STRING "webOS application id")
set(DECODER_BENCH_APP_TITLE "Decoder bench" CACHE STRING "webOS application title")
set(DECODER_BENCH_APP_DESCRIPTION "Native decoder benchmark for rooted LG webOS TVs."
        CACHE STRING "Homebrew Channel application description")
set(DECODER_BENCH_SOURCE_URL "https://github.com/beatcracker/decoder-bench"
        CACHE STRING "Homebrew Channel source URL")
set(DECODER_BENCH_HOMEBREW_ICON_URI
        "https://raw.githubusercontent.com/beatcracker/decoder-bench/main/assets/webos/icon_large.png"
        CACHE STRING "Homebrew Channel icon URL")
set(DECODER_BENCH_ROOT_REQUIRED ON CACHE BOOL "Whether the webOS app requires root")
if (DECODER_BENCH_ROOT_REQUIRED)
    set(DECODER_BENCH_ROOT_REQUIRED_JSON true)
else ()
    set(DECODER_BENCH_ROOT_REQUIRED_JSON false)
endif ()
string(REGEX REPLACE "\\.[^.]*$" "" DECODER_BENCH_PREF_ORG "${DECODER_BENCH_APP_ID}")
string(REGEX REPLACE "^.*\\." "" DECODER_BENCH_PREF_APP "${DECODER_BENCH_APP_ID}")
if (DECODER_BENCH_PREF_ORG STREQUAL DECODER_BENCH_APP_ID OR DECODER_BENCH_PREF_APP STREQUAL "")
    message(FATAL_ERROR "DECODER_BENCH_APP_ID must include an SDL preference organization and app name")
endif ()
set(DECODER_BENCH_ASSET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets/webos")
set(DECODER_BENCH_BUNDLED_BENCH_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets/.local/bench/bundled"
        CACHE PATH "Prebuilt bundled bench root copied into the webOS IPK")

set(DECODER_BENCH_TARGET_WEBOS OFF CACHE BOOL "Build for webOS")
if (DECODER_BENCH_TARGET_WEBOS)
    set(TARGET_WEBOS ON)
    set(OS_LINUX ON)
else ()
    set(OS_LINUX ON)
endif ()

set(FEATURE_FORCE_FULLSCREEN OFF)
set(FEATURE_WINDOW_FULLSCREEN_DESKTOP OFF)
if (TARGET_WEBOS)
    set(THREADS_PTHREAD_ARG "2" CACHE STRING "Result from TRY_RUN" FORCE)
    set(FEATURE_FORCE_FULLSCREEN ON)
endif ()

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)

pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)

if (TARGET_WEBOS)
    pkg_check_modules(PBNJSON_C REQUIRED IMPORTED_TARGET pbnjson_c)
    pkg_check_modules(HELPERS REQUIRED IMPORTED_TARGET helpers)
    pkg_check_modules(NDL_DIRECTMEDIA REQUIRED IMPORTED_TARGET NDL_directmedia)
endif ()

set(DECODER_BENCH_SS4S_MODULE_DIR "${CMAKE_BINARY_DIR}/ss4s-modules")

function(decoder_bench_c_target target)
    set_target_properties(${target} PROPERTIES
            C_STANDARD 11
            C_STANDARD_REQUIRED TRUE)
    if (MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX)
    else ()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
    endif ()
endfunction()

function(decoder_bench_third_party_c_target target)
    set_target_properties(${target} PROPERTIES
            C_STANDARD 11
            C_STANDARD_REQUIRED TRUE)
endfunction()

function(decoder_bench_ss4s_module target module_name)
    set_target_properties(${target} PROPERTIES
            C_STANDARD 11
            C_STANDARD_REQUIRED TRUE
            C_VISIBILITY_PRESET hidden
            PREFIX ""
            LIBRARY_OUTPUT_NAME "ss4s-${module_name}"
            LIBRARY_OUTPUT_DIRECTORY "${DECODER_BENCH_SS4S_MODULE_DIR}"
            RUNTIME_OUTPUT_DIRECTORY "${DECODER_BENCH_SS4S_MODULE_DIR}"
            ARCHIVE_OUTPUT_DIRECTORY "${DECODER_BENCH_SS4S_MODULE_DIR}")
    if (MSVC)
        target_compile_options(${target} PRIVATE /W3)
    else ()
        target_compile_options(${target} PRIVATE -Wall)
    endif ()
endfunction()

add_library(ss4s-module-api INTERFACE)
target_include_directories(ss4s-module-api INTERFACE
        third_party/ss4s/modules/interface/include
        third_party/ss4s/include)

add_library(inih STATIC third_party/inih/ini.c)
target_include_directories(inih PUBLIC third_party/inih)
decoder_bench_third_party_c_target(inih)

if (TARGET_WEBOS)
    if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/opus/CMakeLists.txt")
        message(FATAL_ERROR "Opus source is missing. Run: git submodule update --init --recursive")
    endif ()
    set(OPUS_BUILD_SHARED_LIBRARY OFF CACHE BOOL "Build Opus as a shared library" FORCE)
    set(OPUS_BUILD_TESTING OFF CACHE BOOL "Build Opus tests" FORCE)
    set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "Build Opus programs" FORCE)
    set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "Install Opus pkg-config metadata" FORCE)
    set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "Install Opus CMake package metadata" FORCE)
    set(_DECODER_BENCH_BUILD_TESTING "${BUILD_TESTING}")
    set(BUILD_TESTING OFF)
    add_subdirectory(third_party/opus EXCLUDE_FROM_ALL)
    set(BUILD_TESTING "${_DECODER_BENCH_BUILD_TESTING}")
endif ()

add_library(ss4s STATIC
        third_party/ss4s/src/library.c
        third_party/ss4s/src/driver.c
        third_party/ss4s/src/player.c
        third_party/ss4s/src/audio.c
        third_party/ss4s/src/video.c
        third_party/ss4s/src/module.c
        third_party/ss4s/src/stats.c
        third_party/ss4s/src/feed_guard.c
        third_party/ss4s/src/module_libdl.c
        third_party/ss4s/src/mutex_pthread.c)
target_include_directories(ss4s PUBLIC third_party/ss4s/include)
target_include_directories(ss4s PRIVATE third_party/ss4s/src)
target_link_libraries(ss4s PRIVATE ss4s-module-api Threads::Threads ${CMAKE_DL_LIBS})
target_compile_definitions(ss4s PRIVATE SS4S_LIBRARY_SUFFIX="${CMAKE_SHARED_LIBRARY_SUFFIX}")
set_target_properties(ss4s PROPERTIES
        SS4S_MODULE_LIBRARY_OUTPUT_DIRECTORY "${DECODER_BENCH_SS4S_MODULE_DIR}")
decoder_bench_third_party_c_target(ss4s)

check_include_file("dlfcn.h" HAS_DLFCN)

add_library(ss4s-modules-list-ini SHARED
        third_party/ss4s/modules/list-ini/modules_ini.c)
target_include_directories(ss4s-modules-list-ini PUBLIC
        third_party/ss4s/modules/list-ini/include)
target_include_directories(ss4s-modules-list-ini PRIVATE
        third_party/ss4s/modules/list-ini/include/ss4s)
set_target_properties(ss4s-modules-list-ini PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED TRUE
        PREFIX ""
        LIBRARY_OUTPUT_NAME "ss4s_modules"
        LIBRARY_OUTPUT_DIRECTORY "${DECODER_BENCH_SS4S_MODULE_DIR}"
        RUNTIME_OUTPUT_DIRECTORY "${DECODER_BENCH_SS4S_MODULE_DIR}"
        ARCHIVE_OUTPUT_DIRECTORY "${DECODER_BENCH_SS4S_MODULE_DIR}")
if (HAS_DLFCN)
    target_compile_definitions(ss4s-modules-list-ini PRIVATE _GNU_SOURCE HAS_DLADDR)
    target_link_libraries(ss4s-modules-list-ini PRIVATE ${CMAKE_DL_LIBS})
endif ()

set(DECODER_BENCH_SS4S_MODULE_INI "${DECODER_BENCH_SS4S_MODULE_DIR}/ss4s_modules.ini")
file(MAKE_DIRECTORY "${DECODER_BENCH_SS4S_MODULE_DIR}")
file(WRITE "${DECODER_BENCH_SS4S_MODULE_INI}" "; Generated File - DO NOT EDIT\n")

if (TARGET_WEBOS)
    set(_NDL_DIRECTMEDIA_INCLUDE_DIRS "${NDL_DIRECTMEDIA_INCLUDE_DIRS}")
    set(CMAKE_REQUIRED_INCLUDES ${_NDL_DIRECTMEDIA_INCLUDE_DIRS})
    check_struct_has_member("struct NDL_DIRECTVIDEO_HDR_INFO" transferCharacteristics NDL_directmedia_v2.h
            NDL_DIRECTVIDEO_HDR_INFO_HAS_VUI LANGUAGE C)
    unset(CMAKE_REQUIRED_INCLUDES)
    if (NOT NDL_DIRECTVIDEO_HDR_INFO_HAS_VUI)
        set(_NDL_DIRECTMEDIA_INCLUDE_DIRS third_party/ss4s/mocks/include/libndl-media)
    endif ()

    add_library(ss4s-webos-utils STATIC
            third_party/ss4s/modules/webos/utils/jail_check.c
            third_party/ss4s/modules/webos/utils/highend_check.c
            third_party/ss4s/modules/webos/utils/m3_kadp_fix.c
            third_party/ss4s/modules/webos/utils/max_res.c)
    target_include_directories(ss4s-webos-utils PUBLIC third_party/ss4s/modules/webos/utils)
    target_link_libraries(ss4s-webos-utils PRIVATE ss4s-module-api ${CMAKE_DL_LIBS})
    target_compile_definitions(ss4s-webos-utils PRIVATE _GNU_SOURCE)
    decoder_bench_third_party_c_target(ss4s-webos-utils)

    add_library(ss4s-opus-empty STATIC
            third_party/ss4s/modules/webos/ndl/webos5/opus_empty.c)
    target_include_directories(ss4s-opus-empty PUBLIC third_party/ss4s/modules/webos/ndl/webos5)
    decoder_bench_third_party_c_target(ss4s-opus-empty)

    add_library(ss4s-opus-fix STATIC
            third_party/ss4s/modules/webos/ndl/webos5/opus_fix.c)
    target_include_directories(ss4s-opus-fix PUBLIC third_party/ss4s/modules/webos/ndl/webos5)
    target_link_libraries(ss4s-opus-fix PUBLIC Opus::opus PRIVATE ss4s-module-api)
    decoder_bench_third_party_c_target(ss4s-opus-fix)

    add_library(ss4s-module-ndl-webos5 SHARED
            third_party/ss4s/modules/webos/ndl/webos5/ndl_module.c
            third_party/ss4s/modules/webos/ndl/webos5/ndl_player.c
            third_party/ss4s/modules/webos/ndl/webos5/ndl_audio.c
            third_party/ss4s/modules/webos/ndl/webos5/ndl_video.c
            src/bench_ndl_webos_detect.c)
    target_include_directories(ss4s-module-ndl-webos5 PRIVATE
            third_party/ss4s/modules/webos/ndl/webos5
            third_party/ss4s/modules/webos/utils
            ${_NDL_DIRECTMEDIA_INCLUDE_DIRS})
    target_link_libraries(ss4s-module-ndl-webos5 PRIVATE
            ss4s-module-api
            PkgConfig::NDL_DIRECTMEDIA
            Threads::Threads
            ${CMAKE_DL_LIBS}
            ss4s-opus-empty
            ss4s-opus-fix
            ss4s-webos-utils)
    target_compile_definitions(ss4s-module-ndl-webos5 PRIVATE NDL_DIRECTMEDIA_API_VERSION=2 _GNU_SOURCE)
    target_compile_options(ss4s-module-ndl-webos5 PRIVATE -Wno-error=attributes)
    target_link_options(ss4s-module-ndl-webos5 PRIVATE
            "LINKER:--wrap=NDL_DirectMediaLoad"
            "LINKER:--wrap=NDL_DirectVideoPlay")
    decoder_bench_ss4s_module(ss4s-module-ndl-webos5 ndl-webos5)
    add_dependencies(ss4s ss4s-module-ndl-webos5)
    file(APPEND "${DECODER_BENCH_SS4S_MODULE_INI}"
            "[ndl-webos5]\n"
            "name = webOS NDL\n"
            "group = ndl\n"
            "audio = true\n"
            "video = true\n"
            "weight = 50\n"
            "conflicts = lgnc\n"
            "os_version = >=5\n\n")

    add_library(ss4s-module-ndl-webos4 SHARED
            third_party/ss4s/modules/webos/ndl/webos4/ndl_module.c
            third_party/ss4s/modules/webos/ndl/webos4/ndl_player.c
            third_party/ss4s/modules/webos/ndl/webos4/ndl_audio.c
            third_party/ss4s/modules/webos/ndl/webos4/ndl_video.c)
    target_include_directories(ss4s-module-ndl-webos4 PRIVATE
            third_party/ss4s/modules/webos/ndl/webos4
            third_party/ss4s/modules/webos/utils
            ${_NDL_DIRECTMEDIA_INCLUDE_DIRS})
    target_link_libraries(ss4s-module-ndl-webos4 PRIVATE
            ss4s-module-api
            PkgConfig::NDL_DIRECTMEDIA
            Threads::Threads
            ${CMAKE_DL_LIBS}
            ss4s-webos-utils)
    target_compile_definitions(ss4s-module-ndl-webos4 PRIVATE _GNU_SOURCE)
    target_compile_options(ss4s-module-ndl-webos4 PRIVATE -Wno-error=attributes)
    decoder_bench_ss4s_module(ss4s-module-ndl-webos4 ndl-webos4)
    add_dependencies(ss4s ss4s-module-ndl-webos4)
    file(APPEND "${DECODER_BENCH_SS4S_MODULE_INI}"
            "[ndl-webos4]\n"
            "name = webOS 4 NDL\n"
            "group = ndl\n"
            "audio = true\n"
            "video = true\n"
            "weight = 50\n"
            "conflicts = lgnc\n"
            "os_version = >=3.5,<5\n\n")
else ()
    add_library(ss4s-module-dummy SHARED
            third_party/ss4s/modules/dummy/dummy_module.c
            third_party/ss4s/modules/dummy/dummy_player.c
            third_party/ss4s/modules/dummy/dummy_audio.c
            third_party/ss4s/modules/dummy/dummy_video.c)
    target_include_directories(ss4s-module-dummy PRIVATE third_party/ss4s/modules/dummy)
    target_link_libraries(ss4s-module-dummy PRIVATE ss4s-module-api Threads::Threads)
    decoder_bench_ss4s_module(ss4s-module-dummy dummy)
    add_dependencies(ss4s ss4s-module-dummy)
    file(APPEND "${DECODER_BENCH_SS4S_MODULE_INI}"
            "[dummy]\n"
            "name = Dummy Module\n"
            "audio = true\n"
            "video = true\n"
            "weight = 0\n\n")
endif ()

add_custom_target(ss4s-modules-index
        DEPENDS "${DECODER_BENCH_SS4S_MODULE_INI}" ss4s-modules-list-ini)

add_library(commons-array-list STATIC
        third_party/commons/util/array-list/array_list.c)
target_include_directories(commons-array-list PUBLIC third_party/commons/util/array-list)
decoder_bench_third_party_c_target(commons-array-list)

add_library(commons-version-info STATIC
        third_party/commons/util/version-info/version_info.c)
target_include_directories(commons-version-info PUBLIC third_party/commons/util/version-info)
decoder_bench_third_party_c_target(commons-version-info)

add_library(commons-sps-parser STATIC
        third_party/commons/media/sps-parser/sps_parser.c)
target_include_directories(commons-sps-parser PUBLIC third_party/commons/media/sps-parser)
decoder_bench_third_party_c_target(commons-sps-parser)

if (TARGET_WEBOS)
    add_library(commons-luna-sync STATIC
            third_party/commons/platform/webos/luna-sync/lunasynccall.c)
    target_include_directories(commons-luna-sync PUBLIC third_party/commons/platform/webos/luna-sync)
    target_link_libraries(commons-luna-sync PUBLIC Threads::Threads PkgConfig::PBNJSON_C PkgConfig::HELPERS)
    decoder_bench_third_party_c_target(commons-luna-sync)
endif ()

add_library(commons-os-info STATIC
        third_party/commons/os/info/os_info.c)
target_include_directories(commons-os-info PUBLIC third_party/commons/os/info)
target_link_libraries(commons-os-info PUBLIC commons-version-info)
if (TARGET_WEBOS)
    target_sources(commons-os-info PRIVATE third_party/commons/os/info/os_info_webos.c)
    target_link_libraries(commons-os-info PUBLIC commons-luna-sync PkgConfig::PBNJSON_C)
elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    check_symbol_exists(uname "sys/utsname.h" HAS_UNAME)
    if (HAS_UNAME)
        target_sources(commons-os-info PRIVATE third_party/commons/os/info/os_info_uname.c)
    else ()
        target_sources(commons-os-info PRIVATE third_party/commons/os/info/os_info_generic.c)
    endif ()
else ()
    target_sources(commons-os-info PRIVATE third_party/commons/os/info/os_info_generic.c)
endif ()
decoder_bench_third_party_c_target(commons-os-info)

add_library(commons-ss4s-modules-list STATIC
        third_party/commons/media/ss4s/modules-list/ss4s_modules.c)
target_include_directories(commons-ss4s-modules-list PUBLIC
        third_party/commons/media/ss4s/modules-list)
target_link_libraries(commons-ss4s-modules-list PUBLIC
        ss4s
        ss4s-modules-list-ini
        commons-os-info
        commons-array-list
        inih)
decoder_bench_third_party_c_target(commons-ss4s-modules-list)

configure_file(cmake/config.h.in ${CMAKE_CURRENT_BINARY_DIR}/config.h @ONLY)
configure_file(assets/webos/appinfo.json.in ${CMAKE_CURRENT_BINARY_DIR}/appinfo.json @ONLY)

add_library(bench-core STATIC
        src/bench_annexb.c
        src/bench_path.c
        src/bench_webos_compat.c
        src/bench_decoder_support.c
        src/bench_feeder.c
        src/bench_source.c
        src/bench_stats.c
        src/bench_suite.c)
target_include_directories(bench-core PUBLIC src)
target_include_directories(bench-core PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(bench-core PUBLIC
        ss4s
        commons-sps-parser
        inih
        Threads::Threads)
if (TARGET_WEBOS)
    target_link_libraries(bench-core PUBLIC pthread)
endif ()
decoder_bench_c_target(bench-core)

add_executable(decoder-bench
        src/bench_main.c
        src/bench_platform.c)
target_link_libraries(decoder-bench PRIVATE
        bench-core
        PkgConfig::SDL2
        commons-os-info
        commons-ss4s-modules-list)
target_include_directories(decoder-bench PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
add_dependencies(decoder-bench ss4s-modules-index)

if (TARGET_WEBOS)
    target_compile_definitions(decoder-bench PRIVATE TARGET_WEBOS)
    target_sources(decoder-bench PRIVATE
                        src/bench_platform_webos.c
            src/bench_pbnjson_compat.c
            src/bench_usb_webos.c)
    target_link_libraries(decoder-bench PRIVATE commons-luna-sync ${CMAKE_DL_LIBS} PkgConfig::PBNJSON_C)
endif ()

set_target_properties(decoder-bench PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED TRUE
        INSTALL_RPATH_USE_LINK_PATH TRUE
        INSTALL_RPATH "$ORIGIN/lib")
if (TARGET_WEBOS)
    set_target_properties(decoder-bench PROPERTIES BUILD_WITH_INSTALL_RPATH TRUE)
else ()
    set_target_properties(decoder-bench PROPERTIES
            BUILD_RPATH_USE_ORIGIN TRUE
            BUILD_RPATH "$ORIGIN/ss4s-modules")
endif ()
decoder_bench_c_target(decoder-bench)

if (BUILD_TESTING)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)

    add_library(bench-core-testhooks STATIC
            src/bench_annexb.c
            src/bench_path.c
            src/bench_webos_compat.c
            src/bench_decoder_support.c
            src/bench_feeder.c
            src/bench_source.c
            src/bench_stats.c
            src/bench_suite.c)
    target_include_directories(bench-core-testhooks PUBLIC src)
    target_include_directories(bench-core-testhooks PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
    target_link_libraries(bench-core-testhooks PUBLIC
            ss4s
            commons-sps-parser
            inih
            Threads::Threads)
    if (TARGET_WEBOS)
        target_link_libraries(bench-core-testhooks PUBLIC pthread)
    endif ()
    target_compile_definitions(bench-core-testhooks PUBLIC BENCH_SOURCE_TEST_HOOKS)
    decoder_bench_c_target(bench-core-testhooks)

    add_test(NAME benchplan
            COMMAND Python3::Interpreter -m unittest discover
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/bench" -p "*_test.py")

    add_executable(tests_sps_parser
            third_party/commons/media/sps-parser/sps_parser_tests.c)
    target_link_libraries(tests_sps_parser PRIVATE commons-sps-parser)
    decoder_bench_c_target(tests_sps_parser)
    add_test(NAME commons-sps-parser COMMAND tests_sps_parser)

    add_executable(tests_bench_parser tests/bench_parser_test.c)
    target_link_libraries(tests_bench_parser PRIVATE bench-core-testhooks)
    decoder_bench_c_target(tests_bench_parser)
    add_test(NAME bench-parser COMMAND tests_bench_parser)

    add_executable(tests_bench_path tests/bench_path_test.c)
    target_link_libraries(tests_bench_path PRIVATE bench-core-testhooks)
    decoder_bench_c_target(tests_bench_path)
    add_test(NAME bench-path COMMAND tests_bench_path)

    add_executable(tests_bench_suite tests/bench_suite_test.c)
    target_link_libraries(tests_bench_suite PRIVATE bench-core-testhooks)
    decoder_bench_c_target(tests_bench_suite)
    add_test(NAME bench-suite COMMAND tests_bench_suite)

    add_executable(tests_bench_source_state tests/bench_source_state_test.c)
    target_link_libraries(tests_bench_source_state PRIVATE bench-core-testhooks)
    decoder_bench_c_target(tests_bench_source_state)
    add_test(NAME bench-source-state COMMAND tests_bench_source_state)

        add_executable(tests_bench_stats tests/bench_stats_test.c)
        target_link_libraries(tests_bench_stats PRIVATE bench-core-testhooks)
        decoder_bench_c_target(tests_bench_stats)
        add_test(NAME bench-stats COMMAND tests_bench_stats)

    add_executable(tests_bench_webos_compat tests/bench_webos_compat_test.c)
    target_link_libraries(tests_bench_webos_compat PRIVATE bench-core-testhooks)
    decoder_bench_c_target(tests_bench_webos_compat)
    add_test(NAME bench-webos-compat COMMAND tests_bench_webos_compat)

    add_executable(tests_bench_decoder_support tests/bench_decoder_support_test.c)
    target_link_libraries(tests_bench_decoder_support PRIVATE bench-core-testhooks)
    decoder_bench_c_target(tests_bench_decoder_support)
    add_test(NAME bench-decoder-support COMMAND tests_bench_decoder_support)

    add_executable(tests_bench_video_only_selection tests/bench_video_only_selection_test.c)
    target_link_libraries(tests_bench_video_only_selection PRIVATE commons-ss4s-modules-list)
    target_link_options(tests_bench_video_only_selection PRIVATE "LINKER:--wrap=SS4S_ModuleCheck")
    decoder_bench_c_target(tests_bench_video_only_selection)
    add_test(NAME bench-video-only-selection COMMAND tests_bench_video_only_selection)

    add_executable(tests_bench_ndl_webos_detect
            tests/bench_ndl_webos_detect_test.c
            tests/bench_ndl_webos_detect_real.c
            src/bench_ndl_webos_detect.c)
    target_include_directories(tests_bench_ndl_webos_detect PRIVATE
            third_party/ss4s/mocks/include/libndl-media)
    target_link_libraries(tests_bench_ndl_webos_detect PRIVATE Threads::Threads)
    target_link_options(tests_bench_ndl_webos_detect PRIVATE
            "LINKER:--wrap=NDL_DirectMediaLoad"
            "LINKER:--wrap=NDL_DirectVideoPlay")
    decoder_bench_c_target(tests_bench_ndl_webos_detect)
    target_compile_options(tests_bench_ndl_webos_detect PRIVATE -Wno-error=attributes)
    add_test(NAME bench-ndl-webos-detect COMMAND tests_bench_ndl_webos_detect)

    add_executable(tests_bench_platform_argfilter
            tests/bench_platform_argfilter_test.c
            src/bench_platform.c)
    target_link_libraries(tests_bench_platform_argfilter PRIVATE
            bench-core
            PkgConfig::SDL2
            commons-os-info
            commons-ss4s-modules-list)
    target_compile_definitions(tests_bench_platform_argfilter PRIVATE
            BENCH_PLATFORM_EXPOSE_INTERNALS
            SDL_WEBOS_SCANCODE_BACK=482
            SDL_WEBOS_SCANCODE_EXIT=505)
    target_include_directories(tests_bench_platform_argfilter PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
    decoder_bench_c_target(tests_bench_platform_argfilter)
    add_test(NAME bench-platform-argfilter COMMAND tests_bench_platform_argfilter)

    add_executable(version_info_test
            third_party/commons/util/version-info/version_info_test.c)
    target_link_libraries(version_info_test PRIVATE commons-version-info)
    decoder_bench_c_target(version_info_test)
    add_test(NAME commons-version-info COMMAND version_info_test)
endif ()

if (TARGET_WEBOS)
    set(BENCH_STAGING_DIR "${CMAKE_BINARY_DIR}/staging")
    add_custom_target(decoder-bench-ipk
            COMMAND ${CMAKE_COMMAND}
                    -DDECODER_BENCH_BUNDLED_BENCH_DIR=${DECODER_BENCH_BUNDLED_BENCH_DIR}
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify-bundled-bench.cmake
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${BENCH_STAGING_DIR}/"
            COMMAND ${CMAKE_COMMAND} -E make_directory ${BENCH_STAGING_DIR}/
            COMMAND ${CMAKE_COMMAND} -E make_directory ${BENCH_STAGING_DIR}/bench/suites/
            COMMAND ${CMAKE_COMMAND} -E make_directory ${BENCH_STAGING_DIR}/bench/samples/
            COMMAND ${CMAKE_COMMAND} -E copy_directory ${DECODER_BENCH_SS4S_MODULE_DIR} ${BENCH_STAGING_DIR}/lib
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:decoder-bench> ${BENCH_STAGING_DIR}/
            COMMAND ${CMAKE_COMMAND} -E copy ${DECODER_BENCH_ASSET_DIR}/usb-mount.sh ${BENCH_STAGING_DIR}/
            COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/appinfo.json ${BENCH_STAGING_DIR}/
            COMMAND ${CMAKE_COMMAND} -E copy ${DECODER_BENCH_ASSET_DIR}/icon.png ${BENCH_STAGING_DIR}/
            COMMAND ${CMAKE_COMMAND} -E copy ${DECODER_BENCH_ASSET_DIR}/icon_large.png ${BENCH_STAGING_DIR}/
            COMMAND ${CMAKE_COMMAND} -E copy_directory ${DECODER_BENCH_BUNDLED_BENCH_DIR}/suites ${BENCH_STAGING_DIR}/bench/suites/
            COMMAND ${CMAKE_COMMAND} -E copy_directory ${DECODER_BENCH_BUNDLED_BENCH_DIR}/samples ${BENCH_STAGING_DIR}/bench/samples/
            COMMAND ${CMAKE_COMMAND}
                    -DDECODER_BENCH_STAGING_DIR=${BENCH_STAGING_DIR}
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify-webos-modules.cmake
            COMMAND ares-package ${BENCH_STAGING_DIR}/ -o ${CMAKE_BINARY_DIR}
            COMMAND ${CMAKE_COMMAND}
                    "-DDECODER_BENCH_PACKAGE_DIR=${CMAKE_BINARY_DIR}"
                    "-DDECODER_BENCH_APP_ID=${DECODER_BENCH_APP_ID}"
                    "-DDECODER_BENCH_VERSION=${PROJECT_VERSION}"
                    "-DDECODER_BENCH_APP_TITLE=${DECODER_BENCH_APP_TITLE}"
                    "-DDECODER_BENCH_APP_DESCRIPTION=${DECODER_BENCH_APP_DESCRIPTION}"
                    "-DDECODER_BENCH_HOMEBREW_ICON_URI=${DECODER_BENCH_HOMEBREW_ICON_URI}"
                    "-DDECODER_BENCH_SOURCE_URL=${DECODER_BENCH_SOURCE_URL}"
                    "-DDECODER_BENCH_ROOT_REQUIRED=${DECODER_BENCH_ROOT_REQUIRED}"
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/write-homebrew-manifest.cmake
            DEPENDS decoder-bench
            VERBATIM)
endif ()
