foreach (required_var IN ITEMS
        DECODER_BENCH_PACKAGE_DIR
        DECODER_BENCH_APP_ID
        DECODER_BENCH_VERSION
        DECODER_BENCH_APP_TITLE
        DECODER_BENCH_HOMEBREW_ICON_URI)
    if (NOT DEFINED ${required_var} OR "${${required_var}}" STREQUAL "")
        message(FATAL_ERROR "${required_var} is required")
    endif ()
endforeach ()

if (NOT DEFINED DECODER_BENCH_APP_DESCRIPTION)
    set(DECODER_BENCH_APP_DESCRIPTION "")
endif ()

if (NOT DEFINED DECODER_BENCH_SOURCE_URL)
    set(DECODER_BENCH_SOURCE_URL "")
endif ()

file(GLOB ipk_paths LIST_DIRECTORIES false
        "${DECODER_BENCH_PACKAGE_DIR}/${DECODER_BENCH_APP_ID}_${DECODER_BENCH_VERSION}_*.ipk")
list(LENGTH ipk_paths ipk_count)

if (ipk_count EQUAL 0)
    message(FATAL_ERROR
            "No IPK found for ${DECODER_BENCH_APP_ID} ${DECODER_BENCH_VERSION} in ${DECODER_BENCH_PACKAGE_DIR}")
endif ()

if (ipk_count GREATER 1)
    message(FATAL_ERROR
            "Expected one IPK for ${DECODER_BENCH_APP_ID} ${DECODER_BENCH_VERSION}, found ${ipk_count}: ${ipk_paths}")
endif ()

list(GET ipk_paths 0 ipk_path)
get_filename_component(ipk_name "${ipk_path}" NAME)
file(SHA256 "${ipk_path}" ipk_sha256)
file(SIZE "${ipk_path}" ipk_size)

function(json_quote out value)
    set(escaped "${value}")
    string(REPLACE "\\" "\\\\" escaped "${escaped}")
    string(REPLACE "\"" "\\\"" escaped "${escaped}")
    string(REPLACE "\r" "\\r" escaped "${escaped}")
    string(REPLACE "\n" "\\n" escaped "${escaped}")
    string(REPLACE "\t" "\\t" escaped "${escaped}")
    set(${out} "\"${escaped}\"" PARENT_SCOPE)
endfunction()

json_quote(app_id_json "${DECODER_BENCH_APP_ID}")
json_quote(version_json "${DECODER_BENCH_VERSION}")
json_quote(title_json "${DECODER_BENCH_APP_TITLE}")
json_quote(description_json "${DECODER_BENCH_APP_DESCRIPTION}")
json_quote(icon_uri_json "${DECODER_BENCH_HOMEBREW_ICON_URI}")
json_quote(source_url_json "${DECODER_BENCH_SOURCE_URL}")
json_quote(ipk_name_json "${ipk_name}")
json_quote(ipk_sha256_json "${ipk_sha256}")

if (DECODER_BENCH_ROOT_REQUIRED)
    set(root_required_json true)
else ()
    set(root_required_json false)
endif ()

set(manifest_path "${DECODER_BENCH_PACKAGE_DIR}/${DECODER_BENCH_APP_ID}.manifest.json")
file(WRITE "${manifest_path}"
        "{\n"
        "  \"id\": ${app_id_json},\n"
        "  \"version\": ${version_json},\n"
        "  \"type\": \"native\",\n"
        "  \"title\": ${title_json},\n"
        "  \"appDescription\": ${description_json},\n"
        "  \"iconUri\": ${icon_uri_json},\n"
        "  \"sourceUrl\": ${source_url_json},\n"
        "  \"rootRequired\": ${root_required_json},\n"
        "  \"ipkUrl\": ${ipk_name_json},\n"
        "  \"ipkHash\": {\n"
        "    \"sha256\": ${ipk_sha256_json}\n"
        "  },\n"
        "  \"ipkSize\": ${ipk_size}\n"
        "}\n")

message(STATUS "Wrote Homebrew Channel manifest: ${manifest_path}")
