#include "ome/types/tick_size.hpp"
#include "ome/types/ticks.hpp"

#include <gtest/gtest.h>

namespace {

TEST(Ticks, Arithmetic) {
    EXPECT_EQ((Ticks(100) + Ticks(50)).getValue(), 150);
    EXPECT_EQ((Ticks(100) - Ticks(50)).getValue(), 50);
    EXPECT_EQ((Ticks(100) * 3).getValue(), 300);
    EXPECT_EQ((Ticks(100) / 4).getValue(), 25);
}

TEST(Ticks, Comparisons) {
    EXPECT_LT(Ticks(1), Ticks(2));
    EXPECT_LE(Ticks(2), Ticks(2));
    EXPECT_GT(Ticks(2), Ticks(1));
    EXPECT_GE(Ticks(2), Ticks(2));
    EXPECT_EQ(Ticks(5), Ticks(5));
    EXPECT_NE(Ticks(5), Ticks(6));
}

TEST(TickSize, Equality) {
    EXPECT_EQ(TickSize(0), TickSize(0));
    EXPECT_NE(TickSize(0), TickSize(1));
}

} // namespace
