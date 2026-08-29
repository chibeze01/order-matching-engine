#include "ome/replay/replay_engine.hpp"

#include <cstdio>
#include <exception>

int main(const int argc, char **argv) {
    if (argc != 2 && argc != 3) {
        std::fprintf(stderr, "usage: ome_replay <log> | ome_replay <log_a> <log_b>\n");
        return 2;
    }
    try {
        if (argc == 2) {
            const ReplayResult result = replayLog(argv[1]);
            std::printf("commands=%llu hash=%016llx resting=%llu",
                        static_cast<unsigned long long>(result.command_count),
                        static_cast<unsigned long long>(result.final_hash),
                        static_cast<unsigned long long>(result.resting_count));
            if (result.best_bid_ticks.has_value()) {
                std::printf(" best_bid=%lld", static_cast<long long>(*result.best_bid_ticks));
            }
            if (result.best_ask_ticks.has_value()) {
                std::printf(" best_ask=%lld", static_cast<long long>(*result.best_ask_ticks));
            }
            std::printf("\n");
            return 0;
        }
        const CompareResult result = compareLogs(argv[1], argv[2]);
        if (result.diverged) {
            std::printf("diverged at seq=%llu\n", static_cast<unsigned long long>(result.first_diverging_seq));
            return 1;
        }
        std::printf("match hash=%016llx\n", static_cast<unsigned long long>(result.final_hash_a));
        return 0;
    } catch (const std::exception &ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 2;
    }
}
