#ifndef ORDER_MATCHING_ENGINE_LOG_READER_HPP
#define ORDER_MATCHING_ENGINE_LOG_READER_HPP

#include "ome/replay/log_format.hpp"

#include <cstddef>
#include <fstream>
#include <string>

// Streaming reader for the input log. Reads one record at a time (never
// loads the whole log into memory) and transparently follows rotation across
// part files written by LogWriter.
//
// Corruption is reported, never skipped: a truncated record, a part file
// with a bad or disagreeing header, or a sequence number that does not
// continue from the previous record all throw. Replay depends on having seen
// every command in order, so a partial read must not pass for a clean one.
class LogReader {
  public:
    explicit LogReader(std::string base_path);

    const LogHeader &header() const { return log_header; }

    // Fills command and returns true, or returns false at a clean end of log.
    // Throws std::runtime_error on any corruption (see class comment).
    bool next(Command &command);

  private:
    bool openPart(std::size_t index);
    std::string partPath(std::size_t index) const;

    std::string base_path;
    LogHeader log_header{};
    std::ifstream current_file;
    std::size_t part_index = 0;
    uint64_t expected_seq = 0;
};

#endif // ORDER_MATCHING_ENGINE_LOG_READER_HPP
