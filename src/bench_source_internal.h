#pragma once

#ifdef BENCH_SOURCE_TEST_HOOKS

#include <stdbool.h>
#include <stddef.h>

#include "bench_source.h"

bool bench_source_needs_promotion_for_testing(const BenchSource *source);
bool bench_source_loader_started_for_testing(const BenchSource *source);
bool bench_source_buffer_allocated_for_testing(const BenchSource *source, int index);
bool bench_source_has_acquired_for_testing(const BenchSource *source);
int bench_source_active_buffer_for_testing(const BenchSource *source);
int bench_source_active_au_remaining_for_testing(const BenchSource *source);
bool bench_source_wait_inactive_loaded_for_testing(BenchSource *source, int timeout_ms);
size_t bench_source_carry_capacity_for_testing(const BenchSource *source);

#endif
