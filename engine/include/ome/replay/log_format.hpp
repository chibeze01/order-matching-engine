#ifndef ORDER_MATCHING_ENGINE_LOG_FORMAT_HPP
#define ORDER_MATCHING_ENGINE_LOG_FORMAT_HPP

#include "ome/types/order_type.hpp"
#include "ome/types/side.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iosfwd>

// Wire format for the deterministic input log. Every record is a fixed-width,
// little-endian binary blob so any reader -- this engine, or Engineer B's
// Python analysis tooling -- can parse it without knowing engine internals.
//
// File layout: one LogHeader (LOG_HEADER_SIZE bytes), followed by zero or
// more Command records (LOG_RECORD_SIZE bytes each), back to back. A log can
// span multiple files if it was rotated (see LogWriter); every part file
// repeats the same header.

inline constexpr std::size_t LOG_MAGIC_SIZE = 8;
inline constexpr std::array<char, LOG_MAGIC_SIZE> LOG_MAGIC = {'O', 'M', 'E', 'I', 'L', 'O', 'G', '\0'};
inline constexpr uint32_t LOG_FORMAT_VERSION = 1;
inline constexpr std::size_t LOG_HEADER_SIZE = 48;
inline constexpr std::size_t LOG_RECORD_SIZE = 36;

enum class CommandType : uint8_t { Add = 0, Cancel = 1, Modify = 2 };

// One inbound command, in application order. seq is a strictly increasing
// index assigned by LogWriter; replay uses it to report divergence.
//
// side/price_ticks describe the order's side and price: for Add, the new
// resting order; for Modify, its new side/price after the modify; for
// Cancel, the side/price the order is currently resting at (the cancel call
// itself only needs order_id -- these are carried so replay can inspect the
// affected price level without an extra lookup).
struct Command {
    uint64_t seq = 0;
    CommandType type = CommandType::Add;
    uint64_t order_id = 0;
    Side side = Side::Buy;
    int64_t price_ticks = 0;
    uint64_t quantity = 0;                   // Add/Modify only
    OrderType order_type = OrderType::Limit; // Add only
};

// Per-run config the OrderBook was constructed with, plus the RNG seed used
// by whatever process generated this log (recorded for provenance; replay
// itself only needs the concrete commands, not the seed).
struct LogHeader {
    uint32_t version = LOG_FORMAT_VERSION;
    int64_t min_tick = 0;
    int64_t max_tick = 0;
    uint64_t capacity = 0;
    uint64_t seed = 0;
};

void writeLogHeader(std::ostream &out, const LogHeader &header);
// Returns false (header left unspecified) on short read, bad magic, or
// unsupported version.
bool readLogHeader(std::istream &in, LogHeader &header);

void writeCommand(std::ostream &out, const Command &command);
// Returns false at a clean end of stream (normal end of log) or on a
// truncated record.
bool readCommand(std::istream &in, Command &command);

#endif // ORDER_MATCHING_ENGINE_LOG_FORMAT_HPP
