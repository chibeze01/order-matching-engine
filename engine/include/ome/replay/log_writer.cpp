#include "ome/replay/log_writer.hpp"

#include <stdexcept>
#include <utility>

LogWriter::LogWriter(std::string base_path_, LogHeader header_, const std::size_t max_records_per_file_)
    : base_path(std::move(base_path_)), header(header_), max_records_per_file(max_records_per_file_) {
    openNextPart();
}

std::string LogWriter::partPath(const std::size_t index) const {
    if (index == 0) {
        return base_path;
    }
    return base_path + "." + std::to_string(index);
}

void LogWriter::openNextPart() {
    current_file.open(partPath(part_index), std::ios::binary | std::ios::trunc);
    if (!current_file) {
        throw std::runtime_error("LogWriter: failed to open " + partPath(part_index));
    }
    writeLogHeader(current_file, header);
    if (!current_file) {
        throw std::runtime_error("LogWriter: failed to write header to " + partPath(part_index));
    }
    records_in_current_file = 0;
}

uint64_t LogWriter::append(Command command) {
    if (max_records_per_file > 0 && records_in_current_file >= max_records_per_file) {
        current_file.close();
        ++part_index;
        openNextPart();
    }
    command.seq = next_seq++;
    writeCommand(current_file, command);
    // Checking the stream flag per record is a branch on an already-hot
    // cache line, far cheaper than the write itself. Without it a full disk
    // part-way through a long run yields a silently short log that replays
    // as if it ended cleanly.
    if (!current_file) {
        throw std::runtime_error("LogWriter: failed to write record " + std::to_string(command.seq) + " to " +
                                 partPath(part_index));
    }
    ++records_in_current_file;
    ++total_records;
    return command.seq;
}
