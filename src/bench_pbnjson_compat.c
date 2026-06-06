#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "bench_pbnjson_compat.h"

#include <ctype.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const char *(*resolve_stringify_fn(void))(jvalue_ref) {
    static bool resolved = false;
    static const char *(*fn)(jvalue_ref) = NULL;

    if (!resolved) {
        fn = dlsym(RTLD_DEFAULT, "jvalue_stringify");
        if (fn == NULL) {
            fn = dlsym(RTLD_DEFAULT, "jvalue_tostring_simple");
        }
        resolved = true;
    }

    return fn;
}

static bool bench_pbnjson_accept_object(jvalue_ref object, void *userdata) {
    (void)object;
    (void)userdata;
    return true;
}

const char *bench_pbnjson_stringify(jvalue_ref value) {
    const char *(*fn)(jvalue_ref) = resolve_stringify_fn();
    return fn != NULL ? fn(value) : "{}";
}

char *bench_pbnjson_serialize_alloc(jvalue_ref value) {
    if (!jis_valid(value)) {
        return NULL;
    }

    const char *serialized = bench_pbnjson_stringify(value);
    if (serialized == NULL) {
        return NULL;
    }

    size_t serialized_len = strlen(serialized);
    if (serialized_len == SIZE_MAX) {
        return NULL;
    }

    char *copy = malloc(serialized_len + 1u);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, serialized, serialized_len + 1u);
    return copy;
}

bool bench_pbnjson_is_object(const char *json) {
    return bench_pbnjson_visit_object(json, bench_pbnjson_accept_object, NULL);
}

bool bench_pbnjson_visit_object(const char *json, BenchPbnjsonObjectVisitor visitor, void *userdata) {
    JSchemaInfo schema_info;
    jdomparser_ref parser;
    bool ok = false;

    if (json == NULL || visitor == NULL) {
        return false;
    }

    while (*json != '\0' && isspace((unsigned char)*json)) {
        json++;
    }
    if (*json != '{') {
        return false;
    }

    jschema_info_init(&schema_info, jschema_all(), NULL, NULL);
    parser = jdomparser_create(&schema_info, 0);
    if (parser == NULL) {
        return false;
    }

    size_t json_len = strlen(json);
    if (json_len > (size_t)INT_MAX) {
        jdomparser_release(&parser);
        return false;
    }

    if (jdomparser_feed(parser, json, (int)json_len) && jdomparser_end(parser)) {
        jvalue_ref object = jdomparser_get_result(parser);
        if (jis_object(object)) {
            ok = visitor(object, userdata);
        }
    }

    jdomparser_release(&parser);
    return ok;
}
