#include "ome/book/order_book.hpp"
#include "ome/replay/log_writer.hpp"
#include "ome/replay/replay_engine.hpp"
#include "ome/types/order.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <stdexcept>
#include <string>

namespace {

constexpr uint32_t CENT_TICK_SIZE = 0;
constexpr int64_t BAND_MIN = 100;
constexpr int64_t BAND_MAX = 200;

LogHeader makeHeader() { return LogHeader{LOG_FORMAT_VERSION, CENT_TICK_SIZE, BAND_MIN, BAND_MAX, 32, 7}; }

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

Command modifyCommand(const uint64_t id, const int64_t new_ticks, const uint64_t qty, const Side side) {
    Command c;
    c.type = CommandType::Modify;
    c.order_id = id;
    c.side = side;
    c.price_ticks = new_ticks;
    c.quantity = qty;
    return c;
}

void writeSampleLog(const std::string &path, const uint64_t second_order_qty) {
    LogWriter writer(path, makeHeader());
    writer.append(addCommand(1, 150, 10, Side::Buy));
    writer.append(addCommand(2, 151, second_order_qty, Side::Sell));
    writer.append(cancelCommand(1, 150, Side::Buy));
}

struct LogFileGuard {
    std::string base_path;
    ~LogFileGuard() {
        std::filesystem::remove(base_path);
        for (std::size_t i = 1; std::filesystem::remove(base_path + "." + std::to_string(i)); ++i) {
        }
    }
};

} // namespace

TEST(ReplayEngine, SameLogReplaysToIdenticalHash) {
    const std::string path = "test_replay_same.log";
    const LogFileGuard guard{path};
    writeSampleLog(path, 20);

    const ReplayResult first = replayLog(path);
    const ReplayResult second = replayLog(path);

    EXPECT_EQ(first.command_count, 3u);
    EXPECT_EQ(first.final_hash, second.final_hash);
}

TEST(ReplayEngine, CompareIdenticalLogsReportsNoDivergence) {
    const std::string path_a = "test_replay_cmp_a.log";
    const std::string path_b = "test_replay_cmp_b.log";
    const LogFileGuard guard_a{path_a};
    const LogFileGuard guard_b{path_b};
    writeSampleLog(path_a, 20);
    writeSampleLog(path_b, 20);

    const CompareResult result = compareLogs(path_a, path_b);
    EXPECT_FALSE(result.diverged);
    EXPECT_EQ(result.final_hash_a, result.final_hash_b);
}

TEST(ReplayEngine, CompareDivergentLogsReportsFirstDifferingSeq) {
    const std::string path_a = "test_replay_div_a.log";
    const std::string path_b = "test_replay_div_b.log";
    const LogFileGuard guard_a{path_a};
    const LogFileGuard guard_b{path_b};
    writeSampleLog(path_a, 20); // seq 1 quantity 20
    writeSampleLog(path_b, 99); // seq 1 quantity 99 -- diverges here

    const CompareResult result = compareLogs(path_a, path_b);
    ASSERT_TRUE(result.diverged);
    EXPECT_EQ(result.first_diverging_seq, 1u);
}

TEST(ReplayEngine, CompareRejectsLogsWithDifferentBookConfig) {
    const std::string path_a = "test_replay_cfg_a.log";
    const std::string path_b = "test_replay_cfg_b.log";
    const LogFileGuard guard_a{path_a};
    const LogFileGuard guard_b{path_b};
    writeSampleLog(path_a, 20);
    {
        LogHeader wider = makeHeader();
        wider.max_tick = BAND_MAX + 50; // different band: not comparable
        LogWriter writer(path_b, wider);
        writer.append(addCommand(1, 150, 10, Side::Buy));
    }

    EXPECT_THROW(compareLogs(path_a, path_b), std::runtime_error);
}

TEST(ReplayEngine, ReplayRejectsTruncatedLog) {
    const std::string path = "test_replay_truncated.log";
    const LogFileGuard guard{path};
    writeSampleLog(path, 20);

    const auto size = std::filesystem::file_size(path);
    std::filesystem::resize_file(path, size - 10);

    EXPECT_THROW(replayLog(path), std::runtime_error);
}

// A modify whose target price falls outside the band must leave the order
// resting where it was. Cancelling first and failing the re-insert would drop
// the order from the book entirely.
TEST(ReplayEngine, ModifyOutOfBandLeavesOrderResting) {
    const std::string path = "test_replay_modify_oob.log";
    const LogFileGuard guard{path};
    {
        LogWriter writer(path, makeHeader());
        writer.append(addCommand(1, 150, 10, Side::Buy));
        writer.append(modifyCommand(1, BAND_MAX + 100, 10, Side::Buy)); // out of band
    }

    const ReplayResult replayed = replayLog(path);
    EXPECT_EQ(replayed.command_count, 2u);
    // The order must still be resting at its original price. A cancel-then-
    // failed-reinsert would leave the book empty with no best bid.
    EXPECT_EQ(replayed.resting_count, 1u);
    ASSERT_TRUE(replayed.best_bid_ticks.has_value());
    EXPECT_EQ(*replayed.best_bid_ticks, 150);
}

TEST(ReplayEngine, ModifyInBandMovesOrderToNewLevel) {
    const std::string path = "test_replay_modify_ok.log";
    const LogFileGuard guard{path};
    {
        LogWriter writer(path, makeHeader());
        writer.append(addCommand(1, 150, 10, Side::Buy));
        writer.append(modifyCommand(1, 160, 25, Side::Buy));
    }

    const ReplayResult replayed = replayLog(path);
    EXPECT_EQ(replayed.command_count, 2u);
    // The order moved: still resting, now at the new price with the new size.
    EXPECT_EQ(replayed.resting_count, 1u);
    ASSERT_TRUE(replayed.best_bid_ticks.has_value());
    EXPECT_EQ(*replayed.best_bid_ticks, 160);
}
