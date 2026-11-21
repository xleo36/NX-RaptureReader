#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace zip_util {

/// Streaming ZIP archive reader: parses only the central directory on open()
/// and reads/inflates individual entries on demand without buffering the whole
/// archive in RAM.
class ZipReader {
public:
  bool open(const std::string &path, std::string &err);

  const std::vector<std::string> &entries() const { return entryOrder_; }
  bool hasEntry(const std::string &name) const;
  bool readEntry(const std::string &name, std::string &out,
                 std::string &err) const;
  bool readEntryBytes(const std::string &name, std::vector<uint8_t> &out,
                      std::string &err) const;

private:
  struct ZipEntry {
    uint16_t method = 0;
    uint64_t compressedSize = 0;
    uint64_t uncompressedSize = 0;
    uint64_t localHeaderOffset = 0;
  };

  bool parseCentralDirectory(uint64_t fileSize, std::string &err);
  bool readCompressedEntry(const std::string &name, ZipEntry &meta,
                           std::vector<uint8_t> &compressed,
                           std::string &err) const;

  std::string path_;
  mutable std::ifstream stream_;
  std::vector<std::string> entryOrder_;
  std::unordered_map<std::string, ZipEntry> entries_;
};

} // namespace zip_util
