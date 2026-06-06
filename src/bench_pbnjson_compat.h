#pragma once

#include <stdbool.h>
#include <stddef.h>

#include <pbnjson.h>

typedef bool (*BenchPbnjsonObjectVisitor)(jvalue_ref object, void *userdata);

const char *bench_pbnjson_stringify(jvalue_ref value);
char *bench_pbnjson_serialize_alloc(jvalue_ref value);
bool bench_pbnjson_is_object(const char *json);
bool bench_pbnjson_visit_object(const char *json, BenchPbnjsonObjectVisitor visitor, void *userdata);
