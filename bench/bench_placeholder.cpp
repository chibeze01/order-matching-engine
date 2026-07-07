#include "ome/version.hpp"

#include <benchmark/benchmark.h>

// Placeholder benchmark so the ome_bench target builds and runs from commit
// one. Real matching-engine microbenchmarks (order add, cancel, match) land in
// M2 and M5.
static void BM_LibraryVersion(benchmark::State &state) {
    for (auto _ : state) {
        benchmark::DoNotOptimize(ome::library_version());
    }
}
BENCHMARK(BM_LibraryVersion);

BENCHMARK_MAIN();
