#include "ome/replay/log_format.hpp"

#include <istream>
#include <ostream>

namespace {

void putU8(std::ostream &out, const uint8_t v) { out.put(static_cast<char>(v)); }

void putU32LE(std::ostream &out, const uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        out.put(static_cast<char>((v >> (8 * i)) & 0xFF));
    }
}

void putU64LE(std::ostream &out, const uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        out.put(static_cast<char>((v >> (8 * i)) & 0xFF));
    }
}

void putI64LE(std::ostream &out, const int64_t v) { putU64LE(out, static_cast<uint64_t>(v)); }

bool getU8(std::istream &in, uint8_t &v) {
    char c = 0;
    if (!in.get(c)) {
        return false;
    }
    v = static_cast<uint8_t>(c);
    return true;
}

bool getU32LE(std::istream &in, uint32_t &v) {
    uint32_t result = 0;
    for (int i = 0; i < 4; ++i) {
        char c = 0;
        if (!in.get(c)) {
            return false;
        }
        result |= static_cast<uint32_t>(static_cast<uint8_t>(c)) << (8 * i);
    }
    v = result;
    return true;
}

bool getU64LE(std::istream &in, uint64_t &v) {
    uint64_t result = 0;
    for (int i = 0; i < 8; ++i) {
        char c = 0;
        if (!in.get(c)) {
            return false;
        }
        result |= static_cast<uint64_t>(static_cast<uint8_t>(c)) << (8 * i);
    }
    v = result;
    return true;
}

bool getI64LE(std::istream &in, int64_t &v) {
    uint64_t raw = 0;
    if (!getU64LE(in, raw)) {
        return false;
    }
    v = static_cast<int64_t>(raw);
    return true;
}

} // namespace

void writeLogHeader(std::ostream &out, const LogHeader &header) {
    out.write(LOG_MAGIC.data(), static_cast<std::streamsize>(LOG_MAGIC.size()));
    putU32LE(out, header.version);
    putU32LE(out, 0); // reserved
    putI64LE(out, header.min_tick);
    putI64LE(out, header.max_tick);
    putU64LE(out, header.capacity);
    putU64LE(out, header.seed);
}

bool readLogHeader(std::istream &in, LogHeader &header) {
    std::array<char, LOG_MAGIC_SIZE> magic{};
    if (!in.read(magic.data(), static_cast<std::streamsize>(magic.size()))) {
        return false;
    }
    if (magic != LOG_MAGIC) {
        return false;
    }
    uint32_t version = 0;
    uint32_t reserved = 0;
    if (!getU32LE(in, version) || !getU32LE(in, reserved)) {
        return false;
    }
    if (version != LOG_FORMAT_VERSION) {
        return false;
    }
    int64_t min_tick = 0;
    int64_t max_tick = 0;
    uint64_t capacity = 0;
    uint64_t seed = 0;
    if (!getI64LE(in, min_tick) || !getI64LE(in, max_tick) || !getU64LE(in, capacity) || !getU64LE(in, seed)) {
        return false;
    }
    header = LogHeader{version, min_tick, max_tick, capacity, seed};
    return true;
}

void writeCommand(std::ostream &out, const Command &command) {
    putU64LE(out, command.seq);
    putU8(out, static_cast<uint8_t>(command.type));
    putU8(out, static_cast<uint8_t>(command.side));
    putU8(out, static_cast<uint8_t>(command.order_type));
    putU8(out, 0); // reserved
    putU64LE(out, command.order_id);
    putI64LE(out, command.price_ticks);
    putU64LE(out, command.quantity);
}

bool readCommand(std::istream &in, Command &command) {
    uint64_t seq = 0;
    if (!getU64LE(in, seq)) {
        return false; // clean end of stream at a record boundary
    }
    uint8_t type = 0;
    uint8_t side = 0;
    uint8_t order_type = 0;
    uint8_t reserved = 0;
    if (!getU8(in, type) || !getU8(in, side) || !getU8(in, order_type) || !getU8(in, reserved)) {
        return false;
    }
    uint64_t order_id = 0;
    int64_t price_ticks = 0;
    uint64_t quantity = 0;
    if (!getU64LE(in, order_id) || !getI64LE(in, price_ticks) || !getU64LE(in, quantity)) {
        return false;
    }
    command.seq = seq;
    command.type = static_cast<CommandType>(type);
    command.order_id = order_id;
    command.side = static_cast<Side>(side);
    command.price_ticks = price_ticks;
    command.quantity = quantity;
    command.order_type = static_cast<OrderType>(order_type);
    return true;
}
