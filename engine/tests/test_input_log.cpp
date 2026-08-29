#include "ome/replay/log_reader.hpp"
#include "ome/replay/log_writer.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

namespace {

LogHeader makeHeader() { return LogHeader{LOG_FORMAT_VERSION, 100, 200, 16, 42}; }

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

} // namespace

TEST(InputLog, RoundTripSingleFile) {
    const std::string path = "test_input_log_roundtrip.log";
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

    std::filesystem::remove(path);
}

TEST(InputLog, RotatesAcrossPartFiles) {
    const std::string path = "test_input_log_rotate.log";
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

    std::filesystem::remove(path);
    std::filesystem::remove(path + ".1");
    std::filesystem::remove(path + ".2");
}
