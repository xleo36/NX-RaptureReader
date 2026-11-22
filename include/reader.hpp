#pragma once

#include "epub.hpp"
#include "text_renderer.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace reader {

inline constexpr float HEADING_SCALE = 1.3f;

using Anchor = std::pair<std::size_t, std::size_t>;

struct Line {
  std::string text;
  int y = 0;
  int size = 20;
  bool heading = false;
  /// (block index, byte offset in block) this line starts at
  Anchor origin{0, 0};
  std::vector<epub::StyleSpan> spans;
};

struct Page {
  std::vector<Line> lines;
  /// (block index, byte offset in block) of the first line on this page
  Anchor anchor{0, 0};
  /// Present ONLY on the first page (index 0) of a chapter that has a title;
  /// std::nullopt on every continuation page.
  std::optional<std::string> chapterTitle;
};

struct ChapterPages {
  bool paginated = false;
  std::vector<Page> pages;
};

struct LayoutKey {
  int width = 0;
  int height = 0;
  int bodySize = 0;
  int lineSpacing = 120;

  bool operator==(const LayoutKey &other) const {
    return width == other.width && height == other.height &&
           bodySize == other.bodySize && lineSpacing == other.lineSpacing;
  }
  bool operator!=(const LayoutKey &other) const { return !(*this == other); }
};

class Reader {
public:
  explicit Reader(epub::Book book);

  const epub::Book &book() const { return book_; }

  /// Opens the book at `anchor` (ignored if it no longer fits the book).
  void restore(Anchor anchor);

  /// Current reading position (a place in the text, not a page number, so it
  /// survives font-size, line-spacing, margin, and orientation changes).
  std::optional<Anchor> anchor() const { return bookmark_; }

  /// How far through the book we are, 0..=1000.
  uint32_t permille() const;

  /// Lazily paginates ONLY the active chapter when layout parameters change,
  /// keeping the bookmark.
  void ensureLayout(TextRenderer &text, int width, int height, int bodySize,
                    int lineSpacing = 120);

  const Page *page() const;
  std::pair<std::size_t, std::size_t> pageNumber() const;
  std::pair<std::size_t, std::size_t> chapterNumber() const;

  std::size_t activeChapterPageCount() const;
  std::size_t currentPageInChapter() const { return currentPageInChapter_; }
  std::size_t paginatedChapterCount() const;
  const std::vector<Page> &activeChapterPages() const;

  void nextPage();
  void prevPage();
  void nextChapter();
  void prevChapter();
  void gotoChapter(std::size_t chIdx);

  std::size_t currentChapterIndex() const { return currentChapter_; }
  std::size_t chapterCount() const { return book_.chapters.size(); }
  std::string chapterDisplayTitle(std::size_t chIdx) const;
  uint32_t chapterPercent(std::size_t chIdx) const;

private:
  void mark();
  std::size_t chapterForBlock(std::size_t blockIndex) const;
  void ensureChapterPaginated(std::size_t chIdx);
  std::vector<Page> paginateChapter(std::size_t chIdx, TextRenderer &text,
                                    int width, int height, int bodySize,
                                    int lineSpacing) const;

  epub::Book book_;
  std::vector<ChapterPages> chapterPages_;
  std::size_t currentChapter_ = 0;
  std::size_t currentPageInChapter_ = 0;
  LayoutKey layoutKey_;
  TextRenderer *lastText_ = nullptr;

  /// Remembered spot in the text. Only navigation updates this; re-layouts never do.
  std::optional<Anchor> bookmark_;
  std::vector<std::size_t> cumulative_;
  std::size_t totalBytes_ = 1;
};

} // namespace reader
