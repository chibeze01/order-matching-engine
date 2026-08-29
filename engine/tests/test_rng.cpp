#include "ome/rng.hpp"

#include <gtest/gtest.h>

TEST(Rng, SameSeedProducesSameSequence) {
    Rng a(42);
    Rng b(42);
    for (int i = 0; i < 8; ++i) {
        EXPECT_EQ(a.next(), b.next());
    }
}

TEST(Rng, DifferentSeedsDiverge) {
    Rng a(1);
    Rng b(2);
    bool any_diff = false;
    for (int i = 0; i < 8; ++i) {
        if (a.next() != b.next()) {
            any_diff = true;
        }
    }
    EXPECT_TRUE(any_diff);
}
