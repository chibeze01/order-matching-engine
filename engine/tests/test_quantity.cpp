#include "ome/types/quantity.hpp"

#include <gtest/gtest.h>

namespace {

TEST(Quantity, Arithmetic) {
    EXPECT_EQ((Quantity(100) + Quantity(50)).getValue(), 150);
    EXPECT_EQ((Quantity(100) - Quantity(50)).getValue(), 50);
    EXPECT_EQ((Quantity(100) * 3).getValue(), 300);
    EXPECT_EQ((Quantity(100) / 4).getValue(), 25);
}

TEST(Quantity, Comparisons) {
    EXPECT_LT(Quantity(1), Quantity(2));
    EXPECT_LE(Quantity(2), Quantity(2));
    EXPECT_GT(Quantity(2), Quantity(1));
    EXPECT_GE(Quantity(2), Quantity(2));
    EXPECT_EQ(Quantity(5), Quantity(5));
    EXPECT_NE(Quantity(5), Quantity(6));
}

} // namespace
