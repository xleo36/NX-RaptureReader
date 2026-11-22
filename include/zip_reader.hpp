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
///
/// Archives are untrusted input, so every size and offset in the headers is
/// validated against the real file size before it drives an allocation or a
/// seek, and entries must inflate to exactly their declared size and no more
/// than `MAX_ENTRY_BYTES`.
///
/// Not thread-safe: reads share one file stream (hence `mutable` + `const`
/// accessors). Use one ZipReader per thread.
class ZipReader {
public:
  /// Upper bound on the compressed and uncompressed size of a single entry.
  static constexpr uint64_t MAX_ENTRY_BYTES = 256ull * 1024 * 1024;

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
  uint64_t fileSize_ = 0;
  mutable std::ifstream stream_;
  std::vector<std::string> entryOrder_;
  std::unordered_map<std::string, ZipEntry> entries_;
};

} // namespace zip_util
