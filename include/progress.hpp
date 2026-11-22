#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace progress {

struct Progress {
  /// (block index, byte offset within the block) of the top of the page
  std::pair<std::size_t, std::size_t> anchor{0, 0};
  /// How far through the book, 0..=1000
  uint32_t permille = 0;

  bool operator==(const Progress &other) const {
    return anchor == other.anchor && permille == other.permille;
  }
  bool operator!=(const Progress &other) const { return !(*this == other); }
};

/// Where progress and settings live: the SD card on Switch, `~/.config/nx-rapturereader` elsewhere.
std::string defaultDir();

/// Returns the saved progress for `bookPath`, if any. Accepts older two-field and unhashed files.
std::optional<Progress> load(const std::string &dir,
                             const std::string &bookPath);

bool save(const std::string &dir, const std::string &bookPath,
          const Progress &p, std::string &err);

// Exposed for unit testing
std::string progressFile(const std::string &dir, const std::string &bookPath);
std::string legacyProgressFile(const std::string &dir,
                               const std::string &bookPath);

} // namespace progress
