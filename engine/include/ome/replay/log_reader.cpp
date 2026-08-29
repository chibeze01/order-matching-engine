#include "ome/replay/log_reader.hpp"

#include <stdexcept>
#include <utility>

LogReader::LogReader(std::string base_path_) : base_path(std::move(base_path_)) {
    if (!openPart(0)) {
        throw std::runtime_error("LogReader: failed to open " + base_path);
    }
    if (!readLogHeader(current_file, log_header)) {
        throw std::runtime_error("LogReader: bad header in " + base_path);
    }
}

std::string LogReader::partPath(const std::size_t index) const {
    if (index == 0) {
        return base_path;
    }
    return base_path + "." + std::to_string(index);
}

bool LogReader::openPart(const std::size_t index) {
    current_file.close();
    current_file.clear();
    current_file.open(partPath(index), std::ios::binary);
    return static_cast<bool>(current_file);
}

bool LogReader::next(Command &command) {
    while (true) {
        switch (readCommand(current_file, command)) {
        case ReadStatus::Ok:
            if (command.seq != expected_seq) {
                throw std::runtime_error("LogReader: sequence gap in " + partPath(part_index) + " (expected " +
                                         std::to_string(expected_seq) + ", got " + std::to_string(command.seq) + ")");
            }
            ++expected_seq;
            return true;
        case ReadStatus::Truncated:
            throw std::runtime_error("LogReader: truncated record in " + partPath(part_index) + " at sequence " +
                                     std::to_string(expected_seq));
        case ReadStatus::EndOfLog:
            break; // fall through to the rotation check below
        }

        // Current part exhausted -- continue into the next rotated part if
        // there is one, otherwise this is a clean end of log.
        if (!openPart(part_index + 1)) {
            return false;
        }
        ++part_index;
        LogHeader part_header{};
        if (!readLogHeader(current_file, part_header)) {
            throw std::runtime_error("LogReader: bad header in " + partPath(part_index));
        }
        if (part_header.version != log_header.version || part_header.tick_size != log_header.tick_size ||
            part_header.min_tick != log_header.min_tick || part_header.max_tick != log_header.max_tick ||
            part_header.capacity != log_header.capacity || part_header.seed != log_header.seed) {
            throw std::runtime_error("LogReader: header mismatch in " + partPath(part_index));
        }
    }
}
