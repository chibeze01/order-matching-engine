#include "support/book_fuzz_runner.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string_view>

namespace {

constexpr uint64_t DEFAULT_OPS = 100000;

void printUsage() { std::fprintf(stderr, "usage: ome_book_fuzz [--seed N] [--ops N]\n"); }

uint64_t randomSeed() {
    std::random_device source;
    return (static_cast<uint64_t>(source()) << 32) | static_cast<uint64_t>(source());
}

} // namespace

int main(const int argc, char **argv) {
    uint64_t seed = randomSeed(); // fresh exploration by default; always printed below for reproduction
    uint64_t ops = DEFAULT_OPS;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--seed" && i + 1 < argc) {
            seed = std::strtoull(argv[++i], nullptr, 10);
        } else if (arg == "--ops" && i + 1 < argc) {
            ops = std::strtoull(argv[++i], nullptr, 10);
        } else {
            printUsage();
            return 2;
        }
    }

    std::printf("ome_book_fuzz: seed=%llu ops=%llu\n", static_cast<unsigned long long>(seed),
                static_cast<unsigned long long>(ops));

    const FuzzRunConfig config = defaultFuzzConfig(seed, ops);
    const FuzzRunResult result = runFuzz(config);

    if (!result.ok) {
        std::fprintf(stderr, "ome_book_fuzz: FAIL %s\n", describe(*result.failure).c_str());
        return 1;
    }

    std::printf("ome_book_fuzz: OK ops_executed=%llu\n", static_cast<unsigned long long>(result.ops_executed));
    return 0;
}
