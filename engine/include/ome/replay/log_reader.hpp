#ifndef ORDER_MATCHING_ENGINE_LOG_READER_HPP
#define ORDER_MATCHING_ENGINE_LOG_READER_HPP

#include "ome/replay/log_format.hpp"

#include <cstddef>
#include <fstream>
#include <string>

// Streaming reader for the input log. Reads one record at a time (never
// loads the whole log into memory) and transparently follows rotation across
// part files written by LogWriter.
class LogReader {
  public:
    explicit LogReader(std::string base_path);

    const LogHeader &header() const { return log_header; }

    // Fills command and returns true, or returns false at a clean end of log.
    // Throws std::runtime_error if a part file is missing its header or has a
    // header that disagrees with the first part's.
    bool next(Command &command);

  private:
    bool openPart(std::size_t index);

    std::string base_path;
    LogHeader log_header{};
    std::ifstream current_file;
    std::size_t part_index = 0;
};

#endif // ORDER_MATCHING_ENGINE_LOG_READER_HPP
