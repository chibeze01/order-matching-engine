#include "support/book_fuzz_runner.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

namespace {

constexpr uint64_t NUM_SEEDS = 32;
constexpr uint64_t OPS_PER_SEED = 5000;
constexpr uint64_t PROPERTY_TEST_SWEEP_INTERVAL = 500;

} // namespace

TEST(OrderBookProperty, RandomOpsMatchNaiveReferenceAcrossSeeds) {
    for (uint64_t seed = 1; seed <= NUM_SEEDS; ++seed) {
        FuzzRunConfig config = defaultFuzzConfig(seed, OPS_PER_SEED);
        config.full_sweep_interval = PROPERTY_TEST_SWEEP_INTERVAL;

        const FuzzRunResult result = runFuzz(config);

        ASSERT_TRUE(result.ok) << "seed=" << seed
                               << (result.failure.has_value() ? " " + describe(*result.failure) : std::string());
    }
}
