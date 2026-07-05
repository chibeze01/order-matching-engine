cmake_minimum_required(VERSION 3.25)

project(
    order_matching_engine
    VERSION 0.1.0
    DESCRIPTION "C++20 limit order book matching engine with microstructure simulation"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Opt-in native tuning. Off by default so CI (and other machines) stay portable.
option(OME_ENABLE_NATIVE "Build with -march=native (not portable, off for CI)" OFF)
option(OME_BUILD_TESTS "Build unit tests" ON)
option(OME_BUILD_BENCH "Build benchmarks" ON)

if(OME_ENABLE_NATIVE)
    add_compile_options(-march=native)
endif()

include(FetchContent)

# GoogleTest and Google Benchmark are pulled in via FetchContent so a fresh
# checkout builds with nothing more than a compiler and CMake.
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
    GIT_SHALLOW ON)

set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG v1.9.1
    GIT_SHALLOW ON)

# Engine core. Static library today, one placeholder translation unit so the
# target links from commit one. Real types land in SPA-2.
add_library(ome_core STATIC engine/src/version.cpp)
target_include_directories(ome_core PUBLIC engine/include)
target_compile_features(ome_core PUBLIC cxx_std_20)

if(OME_BUILD_TESTS)
    FetchContent_MakeAvailable(googletest)
    enable_testing()
    include(GoogleTest)
    add_executable(ome_core_tests engine/tests/test_smoke.cpp)
    target_link_libraries(ome_core_tests PRIVATE ome_core GTest::gtest_main)
    gtest_discover_tests(ome_core_tests)
endif()

if(OME_BUILD_BENCH)
    FetchContent_MakeAvailable(benchmark)
    add_executable(ome_bench bench/bench_placeholder.cpp)
    target_link_libraries(ome_bench PRIVATE ome_core benchmark::benchmark)
endif()
