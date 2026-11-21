#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace epub {

enum class BlockKind {
  Heading,
  Paragraph,
};

enum TextStyle : uint8_t {
  StyleRegular = 0,
  StyleItalic = 1 << 0,
  StyleBold = 1 << 1,
};

struct StyleSpan {
  std::size_t start = 0; // byte offset in text
  std::size_t end = 0;   // exclusive byte offset in text
  uint8_t style = StyleRegular;
};

struct Block {
  BlockKind kind = BlockKind::Paragraph;
  std::string text;
  std::vector<StyleSpan> spans;
};

struct Chapter {
  std::string title;
  std::size_t firstBlockIndex = 0;
  std::size_t blockCount = 0;
  std::size_t byteOffset = 0;
  std::size_t byteLength = 0;
};

struct Book {
  std::string title;
  std::string author;
  std::vector<Block> blocks;
  /// Index into `blocks` where each spine document (chapter) begins
  std::vector<std::size_t> chapterStarts;
  /// Chapter metadata aligned 1:1 with `chapterStarts`
  std::vector<Chapter> chapters;
};

struct Metadata {
  std::string title;
  std::string author;
};

/// Loads a full EPUB: container.xml -> OPF -> spine -> XHTML -> text blocks.
std::optional<Book> load(const std::string &path, std::string &err);

/// Reads only title and author (fast: skips chapter parsing). Used by the library.
std::optional<Metadata> metadata(const std::string &path, std::string &err);

/// Extracts raw cover image bytes (OPF cover-image / meta cover / fallback).
bool extractCoverImage(const std::string &path, std::vector<uint8_t> &out,
                       std::string &err);

// Exposed for unit testing and shared helpers
std::string decodeEntities(const std::string &s);
std::string urlDecode(const std::string &s);
std::string resolveZipPath(const std::string &baseDir, const std::string &href);
std::vector<Block> htmlToBlocks(const std::string &html);

} // namespace epub
