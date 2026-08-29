#include "ome/replay/log_reader.hpp"
#include "ome/replay/log_writer.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr uint32_t CENT_TICK_SIZE = 0;

LogHeader makeHeader() { return LogHeader{LOG_FORMAT_VERSION, CENT_TICK_SIZE, 100, 200, 16, 42}; }

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

Command cancelCommand(const uint64_t id) {
    Command c;
    c.type = CommandType::Cancel;
    c.order_id = id;
    return c;
}

// Opens and immediately closes a log, leaving a file with a header and no
// records. Written as a helper rather than a bare scope block because
// clang-format 18 and 22 disagree on how to lay that block out.
void writeHeaderOnlyLog(const std::string &path) { LogWriter writer(path, makeHeader()); }

// Removes the log and any rotated parts when the test scope exits, so a
// failing assertion cannot leave stray files behind for the next run.
struct LogFileGuard {
    std::string base_path;
    ~LogFileGuard() {
        std::filesystem::remove(base_path);
        for (std::size_t i = 1; std::filesystem::remove(base_path + "." + std::to_string(i)); ++i) {
        }
    }
};

} // namespace

TEST(InputLog, RoundTripSingleFile) {
    const std::string path = "test_input_log_roundtrip.log";
    const LogFileGuard guard{path};
    {
        LogWriter writer(path, makeHeader());
        writer.append(addCommand(1, 150, 10, Side::Buy));
        writer.append(addCommand(2, 151, 20, Side::Sell));
        writer.append(cancelCommand(1));
    }

    LogReader reader(path);
    EXPECT_EQ(reader.header().min_tick, 100);
    EXPECT_EQ(reader.header().max_tick, 200);
    EXPECT_EQ(reader.header().capacity, 16u);
    EXPECT_EQ(reader.header().seed, 42u);
    EXPECT_EQ(reader.header().tick_size, CENT_TICK_SIZE);

    Command c;
    ASSERT_TRUE(reader.next(c));
    EXPECT_EQ(c.seq, 0u);
    EXPECT_EQ(c.type, CommandType::Add);
    EXPECT_EQ(c.order_id, 1u);
    EXPECT_EQ(c.price_ticks, 150);
    EXPECT_EQ(c.quantity, 10u);
    EXPECT_EQ(c.side, Side::Buy);

    ASSERT_TRUE(reader.next(c));
    EXPECT_EQ(c.seq, 1u);
    EXPECT_EQ(c.order_id, 2u);
    EXPECT_EQ(c.side, Side::Sell);

    ASSERT_TRUE(reader.next(c));
    EXPECT_EQ(c.seq, 2u);
    EXPECT_EQ(c.type, CommandType::Cancel);
    EXPECT_EQ(c.order_id, 1u);

    EXPECT_FALSE(reader.next(c));
}

TEST(InputLog, HeaderCarriesNonZeroTickSize) {
    const std::string path = "test_input_log_ticksize.log";
    const LogFileGuard guard{path};
    {
        LogHeader header = makeHeader();
        header.tick_size = 3;
        LogWriter writer(path, header);
        writer.append(addCommand(1, 150, 10, Side::Buy));
    }

    LogReader reader(path);
    EXPECT_EQ(reader.header().tick_size, 3u);
}

TEST(InputLog, RotatesAcrossPartFiles) {
    const std::string path = "test_input_log_rotate.log";
    const LogFileGuard guard{path};
    {
        LogWriter writer(path, makeHeader(), /*max_records_per_file=*/2);
        for (uint64_t i = 0; i < 5; ++i) {
            writer.append(addCommand(i + 1, 150, 10, Side::Buy));
        }
    }

    ASSERT_TRUE(std::filesystem::exists(path));
    ASSERT_TRUE(std::filesystem::exists(path + ".1"));
    ASSERT_TRUE(std::filesystem::exists(path + ".2"));
    EXPECT_FALSE(std::filesystem::exists(path + ".3"));

    LogReader reader(path);
    std::vector<uint64_t> seqs;
    Command c;
    while (reader.next(c)) {
        seqs.push_back(c.seq);
    }
    EXPECT_EQ(seqs, (std::vector<uint64_t>{0, 1, 2, 3, 4}));
}

TEST(InputLog, TruncatedRecordIsRejectedNotTreatedAsEndOfLog) {
    const std::string path = "test_input_log_truncated.log";
    const LogFileGuard guard{path};
    {
        LogWriter writer(path, makeHeader());
        writer.append(addCommand(1, 150, 10, Side::Buy));
        writer.append(addCommand(2, 151, 20, Side::Sell));
    }

    // Chop the tail off the final record so the log ends mid-record.
    const auto size = std::filesystem::file_size(path);
    std::filesystem::resize_file(path, size - 10);

    LogReader reader(path);
    Command c;
    ASSERT_TRUE(reader.next(c)); // first record is intact
    EXPECT_EQ(c.seq, 0u);
    EXPECT_THROW(reader.next(c), std::runtime_error);
}

TEST(InputLog, HeaderOnlyLogReadsAsCleanEndOfLog) {
    const std::string path = "test_input_log_empty.log";
    const LogFileGuard guard{path};
    writeHeaderOnlyLog(path);

    LogReader reader(path);
    Command c;
    EXPECT_FALSE(reader.next(c));
}

TEST(InputLog, SequenceGapIsRejected) {
    const std::string path = "test_input_log_seqgap.log";
    const LogFileGuard guard{path};
    {
        // Write records directly so the sequence numbers skip 1.
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        writeLogHeader(out, makeHeader());
        Command first = addCommand(1, 150, 10, Side::Buy);
        first.seq = 0;
        writeCommand(out, first);
        Command second = addCommand(2, 151, 20, Side::Sell);
        second.seq = 2; // gap: 1 is missing
        writeCommand(out, second);
    }

    LogReader reader(path);
    Command c;
    ASSERT_TRUE(reader.next(c));
    EXPECT_THROW(reader.next(c), std::runtime_error);
}
