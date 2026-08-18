if (NOT DEFINED DECODER_BENCH_STAGING_DIR OR DECODER_BENCH_STAGING_DIR STREQUAL "")
    message(FATAL_ERROR "DECODER_BENCH_STAGING_DIR is not set")
endif ()

set(_decoder_bench_module_dir "${DECODER_BENCH_STAGING_DIR}/lib")
set(_decoder_bench_module_index "${_decoder_bench_module_dir}/ss4s_modules.ini")

foreach (_decoder_bench_module IN ITEMS ss4s-ndl-webos4.so ss4s-ndl-webos5.so)
    if (NOT EXISTS "${_decoder_bench_module_dir}/${_decoder_bench_module}")
        message(FATAL_ERROR "Staged webOS package is missing lib/${_decoder_bench_module}")
    endif ()
endforeach ()

if (NOT EXISTS "${_decoder_bench_module_index}")
    message(FATAL_ERROR "Staged webOS package is missing lib/ss4s_modules.ini")
endif ()

file(READ "${_decoder_bench_module_index}" _decoder_bench_module_index_contents)

foreach (_decoder_bench_module_id IN ITEMS ndl-webos4 ndl-webos5)
    string(REGEX MATCHALL "\\[${_decoder_bench_module_id}\\]"
            _decoder_bench_module_sections "${_decoder_bench_module_index_contents}")
    list(LENGTH _decoder_bench_module_sections _decoder_bench_module_section_count)
    if (NOT _decoder_bench_module_section_count EQUAL 1)
        message(FATAL_ERROR
                "Staged module index must contain exactly one [${_decoder_bench_module_id}] entry")
    endif ()
endforeach ()

string(FIND "${_decoder_bench_module_index_contents}"
        "[ndl-webos4]\nname = webOS 4 NDL\ngroup = ndl\naudio = true\nvideo = true\nweight = 50\nconflicts = lgnc\nos_version = >=3.5,<5\n"
        _decoder_bench_webos4_entry)
if (_decoder_bench_webos4_entry EQUAL -1)
    message(FATAL_ERROR
            "Staged ndl-webos4 index entry must use the ndl group and os_version >=3.5,<5")
endif ()

string(FIND "${_decoder_bench_module_index_contents}"
        "[ndl-webos5]\nname = webOS NDL\ngroup = ndl\naudio = true\nvideo = true\nweight = 50\nconflicts = lgnc\nos_version = >=5\n"
        _decoder_bench_webos5_entry)
if (_decoder_bench_webos5_entry EQUAL -1)
    message(FATAL_ERROR
            "Staged ndl-webos5 index entry must use the ndl group and os_version >=5")
endif ()

message(STATUS "Verified staged NDL v1/v2 modules and mutually exclusive OS-version routes")
