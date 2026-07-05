#include "ome/types.hpp"

#include <gtest/gtest.h>

#include <string_view>

// Trivial passing test so CI is green from commit one. Real book and matching
// tests replace this starting in SPA-2.
TEST(Smoke, LibraryVersionIsReported) { EXPECT_EQ(std::string_view{ome::library_version()}, "0.1.0-dev"); }
