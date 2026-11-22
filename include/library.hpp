#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace library {

enum class Format {
  Epub,
  Cbz,
  Pdf,
};

struct Entry {
  std::string path;
  std::string title;
  std::string author;
  Format format = Format::Epub;
  /// Reading progress 0..=1000, or std::nullopt if never opened
  std::optional<uint32_t> permille;
};

/// Returns true if `path` has a `.cbz` extension (case-insensitive).
bool isComicPath(const std::string &path);

/// Returns true if `path` has a `.pdf` extension (case-insensitive).
bool isPdfPath(const std::string &path);

/// Folders searched when no path is given on the command line.
std::vector<std::string> defaultSources();

/// Collects every `.epub`, `.cbz`, and `.pdf` in the given files/folders (non-recursive), sorted by title.
std::vector<Entry> scan(const std::vector<std::string> &sources,
                        const std::string &progressDir);

/// Re-reads the saved progress of every entry (call after reading a book).
void refreshProgress(std::vector<Entry> &entries,
                     const std::string &progressDir);

} // namespace library
