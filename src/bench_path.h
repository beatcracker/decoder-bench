#pragma once

#include <stdbool.h>
#include <stddef.h>

/*
 * Tiny POSIX-style syntactic path helpers for decoder-bench.
 *
 * These helpers do not query the filesystem, canonicalize paths, resolve
 * symlinks, or implement Windows drive/UNC semantics. They emit and recognize
 * '/' as the path separator. Checked copy/join operations tolerate overlapping
 * input and output buffers.
 *
 * bench_path_join treats the right-hand side as a child path. A leading
 * separator on the child is tolerated and collapsed into the join; it does
 * not make the child replace the base path.
 */
int bench_path_copy(char *dst, size_t dst_size, const char *src);
int bench_path_join(char *dst, size_t dst_size, const char *left, const char *right);
char *bench_path_join_alloc(const char *left, const char *right);
int bench_path_dir_ensure(const char *dir);
int bench_path_sanitize_filename_component(char *dst, size_t dst_size, const char *src);
int bench_path_readlink(char *dst, size_t dst_size, const char *link_path);

/*
 * Replace path with its parent directory.
 *
 * Returns true and mutates path on success. Returns false and leaves path
 * unchanged if no parent is available.
 */
bool bench_path_parent(char *path);

/*
 * Return a pointer to the last path component within path. Trailing separators
 * are significant, so "foo/" returns an empty basename.
 */
const char *bench_path_basename(const char *path);
