#include "ome/replay/log_writer.hpp"
#include "ome/replay/replay_engine.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

namespace {

LogHeader makeHeader() { return LogHeader{LOG_FORMAT_VERSION, 100, 200, 32, 7}; }

Command addCommand(const uint64_t id, const int64_t ticks, const uint64_t qty, const Side side) {
    Command c;
    c.type = CommandType::Add;
    c.order_id = id;
    c.side = side;
    c.price_ticks = ticks;
    c.quantity = qty;
    c.order_type = OrderType::Limit;
    return c;
}

Command cancelCommand(const uint64_t id, const int64_t ticks, const Side side) {
    Command c;
    c.type = CommandType::Cancel;
    c.order_id = id;
    c.side = side;
    c.price_ticks = ticks;
    return c;
}

void writeSampleLog(const std::string &path, const uint64_t second_order_qty) {
    LogWriter writer(path, makeHeader());
    writer.append(addCommand(1, 150, 10, Side::Buy));
    writer.append(addCommand(2, 151, second_order_qty, Side::Sell));
    writer.append(cancelCommand(1, 150, Side::Buy));
}

} // namespace

TEST(ReplayEngine, SameLogReplaysToIdenticalHash) {
    const std::string path = "test_replay_same.log";
    writeSampleLog(path, 20);

    const ReplayResult first = replayLog(path);
    const ReplayResult second = replayLog(path);

    EXPECT_EQ(first.command_count, 3u);
    EXPECT_EQ(first.final_hash, second.final_hash);

    std::filesystem::remove(path);
}

TEST(ReplayEngine, CompareIdenticalLogsReportsNoDivergence) {
    const std::string path_a = "test_replay_cmp_a.log";
    const std::string path_b = "test_replay_cmp_b.log";
    writeSampleLog(path_a, 20);
    writeSampleLog(path_b, 20);

    const CompareResult result = compareLogs(path_a, path_b);
    EXPECT_FALSE(result.diverged);
    EXPECT_EQ(result.final_hash_a, result.final_hash_b);

    std::filesystem::remove(path_a);
    std::filesystem::remove(path_b);
}

TEST(ReplayEngine, CompareDivergentLogsReportsFirstDifferingSeq) {
    const std::string path_a = "test_replay_div_a.log";
    const std::string path_b = "test_replay_div_b.log";
    writeSampleLog(path_a, 20); // seq 1 quantity 20
    writeSampleLog(path_b, 99); // seq 1 quantity 99 -- diverges here

    const CompareResult result = compareLogs(path_a, path_b);
    ASSERT_TRUE(result.diverged);
    EXPECT_EQ(result.first_diverging_seq, 1u);

    std::filesystem::remove(path_a);
    std::filesystem::remove(path_b);
}
