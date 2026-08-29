#include "ome/replay/log_format.hpp"

#include <istream>
#include <ostream>

namespace {

// Records are encoded into a fixed-size buffer and written in one call, and
// decoded the same way, so a short read is detectable as a byte count rather
// than as a stream failure part-way through a field.

void putU8(char *buf, std::size_t &offset, const uint8_t v) { buf[offset++] = static_cast<char>(v); }

void putU32LE(char *buf, std::size_t &offset, const uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        buf[offset++] = static_cast<char>((v >> (8 * i)) & 0xFF);
    }
}

void putU64LE(char *buf, std::size_t &offset, const uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        buf[offset++] = static_cast<char>((v >> (8 * i)) & 0xFF);
    }
}

void putI64LE(char *buf, std::size_t &offset, const int64_t v) { putU64LE(buf, offset, static_cast<uint64_t>(v)); }

uint8_t getU8(const char *buf, std::size_t &offset) { return static_cast<uint8_t>(buf[offset++]); }

uint32_t getU32LE(const char *buf, std::size_t &offset) {
    uint32_t result = 0;
    for (int i = 0; i < 4; ++i) {
        result |= static_cast<uint32_t>(static_cast<uint8_t>(buf[offset++])) << (8 * i);
    }
    return result;
}

uint64_t getU64LE(const char *buf, std::size_t &offset) {
    uint64_t result = 0;
    for (int i = 0; i < 8; ++i) {
        result |= static_cast<uint64_t>(static_cast<uint8_t>(buf[offset++])) << (8 * i);
    }
    return result;
}

int64_t getI64LE(const char *buf, std::size_t &offset) { return static_cast<int64_t>(getU64LE(buf, offset)); }

} // namespace

void writeLogHeader(std::ostream &out, const LogHeader &header) {
    std::array<char, LOG_HEADER_SIZE> buf{};
    std::size_t offset = 0;
    for (const char c : LOG_MAGIC) {
        buf[offset++] = c;
    }
    putU32LE(buf.data(), offset, header.version);
    putU32LE(buf.data(), offset, header.tick_size);
    putI64LE(buf.data(), offset, header.min_tick);
    putI64LE(buf.data(), offset, header.max_tick);
    putU64LE(buf.data(), offset, header.capacity);
    putU64LE(buf.data(), offset, header.seed);
    out.write(buf.data(), static_cast<std::streamsize>(buf.size()));
}

bool readLogHeader(std::istream &in, LogHeader &header) {
    std::array<char, LOG_HEADER_SIZE> buf{};
    in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
    if (in.gcount() != static_cast<std::streamsize>(buf.size())) {
        return false;
    }
    std::size_t offset = 0;
    for (const char expected : LOG_MAGIC) {
        if (buf[offset++] != expected) {
            return false;
        }
    }
    LogHeader parsed;
    parsed.version = getU32LE(buf.data(), offset);
    if (parsed.version != LOG_FORMAT_VERSION) {
        return false;
    }
    parsed.tick_size = getU32LE(buf.data(), offset);
    parsed.min_tick = getI64LE(buf.data(), offset);
    parsed.max_tick = getI64LE(buf.data(), offset);
    parsed.capacity = getU64LE(buf.data(), offset);
    parsed.seed = getU64LE(buf.data(), offset);
    header = parsed;
    return true;
}

void writeCommand(std::ostream &out, const Command &command) {
    std::array<char, LOG_RECORD_SIZE> buf{};
    std::size_t offset = 0;
    putU64LE(buf.data(), offset, command.seq);
    putU8(buf.data(), offset, static_cast<uint8_t>(command.type));
    putU8(buf.data(), offset, static_cast<uint8_t>(command.side));
    putU8(buf.data(), offset, static_cast<uint8_t>(command.order_type));
    putU8(buf.data(), offset, 0); // reserved
    putU64LE(buf.data(), offset, command.order_id);
    putI64LE(buf.data(), offset, command.price_ticks);
    putU64LE(buf.data(), offset, command.quantity);
    out.write(buf.data(), static_cast<std::streamsize>(buf.size()));
}

ReadStatus readCommand(std::istream &in, Command &command) {
    std::array<char, LOG_RECORD_SIZE> buf{};
    in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
    const std::streamsize got = in.gcount();
    if (got == 0) {
        return ReadStatus::EndOfLog; // ended exactly on a record boundary
    }
    if (got != static_cast<std::streamsize>(buf.size())) {
        return ReadStatus::Truncated; // partial record: the log was cut short
    }

    std::size_t offset = 0;
    command.seq = getU64LE(buf.data(), offset);
    command.type = static_cast<CommandType>(getU8(buf.data(), offset));
    command.side = static_cast<Side>(getU8(buf.data(), offset));
    command.order_type = static_cast<OrderType>(getU8(buf.data(), offset));
    getU8(buf.data(), offset); // reserved
    command.order_id = getU64LE(buf.data(), offset);
    command.price_ticks = getI64LE(buf.data(), offset);
    command.quantity = getU64LE(buf.data(), offset);
    return ReadStatus::Ok;
}
