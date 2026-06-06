#pragma once

#include "bench_types.h"

/**
 * Load a .bench suite file. Validates required keys and resolves sample path
 * strings relative to the sibling samples/ directory. Fixture existence is
 * checked later by source open or prefetch.
 *
 * @param path Absolute path to the .bench file.
 * @param suite Output suite struct. Cleared before parsing and left empty on error.
 * @return 0 on success, non-zero on syntax, required-key, or path-length error.
 */
int bench_suite_load(const char *path, BenchSuite *suite);

/**
 * Discover available .bench suite files in the given bench root directory.
 * Looks in <root>/suites/ for *.bench files.
 *
 * @param bench_root Root bench directory.
 * @param names Output array of suite names (without .bench extension). Caller owns memory.
 * @param max_names Maximum number of names to return.
 * @return Number of suites found, or -1 on error.
 */
int bench_suite_discover(const char *bench_root, char names[][BENCH_MAX_NAME_LEN], int max_names);
