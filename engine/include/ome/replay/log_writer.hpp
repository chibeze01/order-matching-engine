#ifndef ORDER_MATCHING_ENGINE_LOG_WRITER_HPP
#define ORDER_MATCHING_ENGINE_LOG_WRITER_HPP

#include "ome/replay/log_format.hpp"

#include <cstddef>
#include <fstream>
#include <string>

// Append-only writer for the input log. Streams records straight to disk (no
// in-memory buffering of the whole log), and rotates to a new part file once
// max_records_per_file records have been written, so a single file never
// grows past that count even across 10M+ event runs. Every part file repeats
// the header so it is independently parseable.
//
// Part files are named base_path, base_path.1, base_path.2, ... in write
// order. max_records_per_file == 0 means never rotate (single file).
class LogWriter {
  public:
    LogWriter(std::string base_path, LogHeader header, std::size_t max_records_per_file = 0);

    // Stamps command.seq with the next sequence number, writes it, and
    // returns the assigned seq.
    uint64_t append(Command command);

    std::size_t recordCount() const { return total_records; }

  private:
    void openNextPart();
    std::string partPath(std::size_t index) const;

    std::string base_path;
    LogHeader header;
    std::size_t max_records_per_file;
    std::ofstream current_file;
    std::size_t part_index = 0;
    std::size_t records_in_current_file = 0;
    std::size_t total_records = 0;
    uint64_t next_seq = 0;
};

#endif // ORDER_MATCHING_ENGINE_LOG_WRITER_HPP
