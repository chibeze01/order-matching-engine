#include "ome/version.hpp"

#include <gtest/gtest.h>

#include <string_view>

TEST(Smoke, LibraryVersionIsReported) { EXPECT_EQ(std::string_view{ome::library_version()}, "0.1.0-dev"); }
