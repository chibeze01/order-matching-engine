#include "ome/replay/log_reader.hpp"

#include <stdexcept>
#include <utility>

namespace {
std::string partPath(const std::string &base_path, const std::size_t index) {
    if (index == 0) {
        return base_path;
    }
    return base_path + "." + std::to_string(index);
}
} // namespace

LogReader::LogReader(std::string base_path_) : base_path(std::move(base_path_)) {
    if (!openPart(0)) {
        throw std::runtime_error("LogReader: failed to open " + base_path);
    }
    if (!readLogHeader(current_file, log_header)) {
        throw std::runtime_error("LogReader: bad header in " + base_path);
    }
}

bool LogReader::openPart(const std::size_t index) {
    current_file.close();
    current_file.clear();
    current_file.open(partPath(base_path, index), std::ios::binary);
    return static_cast<bool>(current_file);
}

bool LogReader::next(Command &command) {
    if (readCommand(current_file, command)) {
        return true;
    }
    // Current part exhausted (or had no records past its header) -- try the
    // next rotated part.
    ++part_index;
    if (!openPart(part_index)) {
        return false; // no more parts: clean end of log
    }
    LogHeader part_header{};
    if (!readLogHeader(current_file, part_header)) {
        throw std::runtime_error("LogReader: bad header in " + partPath(base_path, part_index));
    }
    if (part_header.version != log_header.version || part_header.min_tick != log_header.min_tick ||
        part_header.max_tick != log_header.max_tick || part_header.capacity != log_header.capacity ||
        part_header.seed != log_header.seed) {
        throw std::runtime_error("LogReader: header mismatch in " + partPath(base_path, part_index));
    }
    return next(command);
}
