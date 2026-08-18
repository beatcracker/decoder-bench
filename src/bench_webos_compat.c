#include "bench_webos_compat.h"

const char *bench_webos_ndl_module_id(int sdk_major, int sdk_minor) {
    if (sdk_major >= 5) {
        return "ndl-webos5";
    }
    if (sdk_major == 4) {
        return "ndl-webos4";
    }
    if (sdk_major == 3 && sdk_minor >= 5) {
        return "ndl-webos4";
    }
    return NULL;
}
