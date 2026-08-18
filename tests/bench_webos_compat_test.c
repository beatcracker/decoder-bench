#include "bench_webos_compat.h"

#include <assert.h>
#include <string.h>

int main(void) {
    const char *webos4 = bench_webos_ndl_module_id(4, 10);
    const char *webos5 = bench_webos_ndl_module_id(5, 0);
    const char *newer = bench_webos_ndl_module_id(7, 3);

    assert(webos4 != NULL && strcmp(webos4, "ndl-webos4") == 0);
    assert(webos5 != NULL && strcmp(webos5, "ndl-webos5") == 0);
    assert(newer != NULL && strcmp(newer, "ndl-webos5") == 0);
    assert(bench_webos_ndl_module_id(0, 0) == NULL);
    assert(bench_webos_ndl_module_id(-1, -1) == NULL);
    assert(bench_webos_ndl_module_id(3, 4) == NULL);
    return 0;
}
