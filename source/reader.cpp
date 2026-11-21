#include "reader.hpp"

#include <algorithm>
#include <cmath>

namespace reader {

Reader::Reader(epub::Book book) : book_(std::move(book)) {
  // Synthesize chapters if the caller constructed Book without populating `chapters`
  if (book_.chapters.empty() && !book_.blocks.empty()) {
    std::vector<std::size_t> starts = book_.chapterStarts;
    if (starts.empty()) {
      starts.push_back(0);
    }
    std::size_t runningBytes = 0;
    for (std::size_t i = 0; i < starts.size(); ++i) {
      std::size_t begin = starts[i];
      std::size_t end =
          (i + 1 < starts.size()) ? starts[i + 1] : book_.blocks.size();
      if (begin >= book_.blocks.size()) {
        continue;
      }
      end = std::min(end, book_.blocks.size());
      epub::Chapter ch;
      ch.firstBlockIndex = begin;
      ch.blockCount = end - begin;
      ch.byteOffset = runningBytes;
      if (ch.blockCount > 0 &&
          book_.blocks[begin].kind == epub::BlockKind::Heading) {
        ch.title = book_.blocks[begin].text;
      }
      for (std::size_t bi = begin; bi < end; ++bi) {
        ch.byteLength += book_.blocks[bi].text.size();
      }
      runningBytes += ch.byteLength;
      book_.chapters.push_back(std::move(ch));
    }
    book_.chapterStarts = std::move(starts);
  }

  chapterPages_.resize(book_.chapters.size());

  cumulative_.reserve(book_.blocks.size());
  std::size_t total = 0;
  for (const auto &b : book_.blocks) {
    cumulative_.push_back(total);
    total += b.text.size();
  }
  totalBytes_ = std::max<std::size_t>(1, total);
}

std::vector<Page> Reader::paginateChapter(std::size_t chIdx,
                                          TextRenderer &text, int width,
                                          int height, int bodySize,
                                          int lineSpacing) const {
  std::vector<Page> pages;
  if (chIdx >= book_.chapters.size()) {
    return pages;
  }

  const epub::Chapter &ch = book_.chapters[chIdx];
  std::size_t startBlock = ch.firstBlockIndex;
  std::size_t endBlock =
      std::min(book_.blocks.size(), startBlock + ch.blockCount);
  if (startBlock >= endBlock) {
    return pages;
  }

  int headingSize = static_cast<int>(
      std::round(static_cast<float>(bodySize) * HEADING_SCALE));
  float spacingScale = static_cast<float>(std::clamp(lineSpacing, 100, 180)) /
                       100.0f;
  std::vector<Line> lines;
  int y = 0;
  std::optional<std::string> pendingChapterTitle;
  bool isFirstPageOfChapter = true;

  auto finish = [&]() {
    if (!lines.empty()) {
      Anchor a = lines.front().origin;
      Page p;
      p.lines = std::move(lines);
      p.anchor = a;
      if (isFirstPageOfChapter && pendingChapterTitle) {
        p.chapterTitle = *pendingChapterTitle;
      }
      pages.push_back(std::move(p));
      lines.clear();
      isFirstPageOfChapter = false;
      pendingChapterTitle.reset();
    }
    y = 0;
  };

  std::size_t bodyStart = startBlock;
  // Render the chapter title exclusively on page 0 of this chapter.
  if (!ch.title.empty()) {
    pendingChapterTitle = ch.title;
    std::vector<epub::StyleSpan> titleSpans;
    if (book_.blocks[startBlock].kind == epub::BlockKind::Heading) {
      titleSpans = book_.blocks[startBlock].spans;
    }
    auto [wrappedTitle, titleLh] =
        text.wrapLines(ch.title, headingSize, width, titleSpans);
    for (auto &wl : wrappedTitle) {
      if (y + titleLh > height && !lines.empty()) {
        finish();
      }
      lines.push_back(Line{std::move(wl.text), y, headingSize, true,
                           Anchor{startBlock, wl.byteOffset},
                           std::move(wl.spans)});
      y += titleLh;
    }
    if (!lines.empty()) {
      y += titleLh / 2;
    }
    if (book_.blocks[startBlock].kind == epub::BlockKind::Heading) {
      bodyStart = startBlock + 1;
    }
  }

  for (std::size_t bi = bodyStart; bi < endBlock; ++bi) {
    const auto &block = book_.blocks[bi];
    // Avoid repeating the chapter title if a spine document duplicates it
    if (block.kind == epub::BlockKind::Heading && !ch.title.empty() &&
        block.text == ch.title) {
      continue;
    }

    bool heading = block.kind == epub::BlockKind::Heading;
    int size = heading ? headingSize : bodySize;
    auto [wrapped, lh] = text.wrapLines(block.text, size, width, block.spans);
    if (wrapped.empty()) {
      continue;
    }

    int stepY = heading
                    ? lh
                    : std::max(1, static_cast<int>(std::lround(
                                      static_cast<float>(lh) * spacingScale)));

    // Keep subheadings together with at least two following body lines
    if (heading && !lines.empty() && y + lh * 3 > height) {
      finish();
    }

    if (!lines.empty()) {
      y += heading ? lh : std::max(lh / 2, stepY / 2);
    }

    for (auto &wl : wrapped) {
      if (y + lh > height && !lines.empty()) {
        finish();
      }
      lines.push_back(Line{std::move(wl.text), y, size, heading,
                           Anchor{bi, wl.byteOffset}, std::move(wl.spans)});
      y += stepY;
    }
    if (heading) {
      y += lh / 2;
    }
  }
  finish();
  return pages;
}

std::size_t Reader::chapterForBlock(std::size_t blockIndex) const {
  if (book_.chapters.empty()) {
    return 0;
  }
  for (std::size_t i = book_.chapters.size(); i-- > 0;) {
    if (book_.chapters[i].firstBlockIndex <= blockIndex) {
      return i;
    }
  }
  return 0;
}

void Reader::ensureChapterPaginated(std::size_t chIdx) {
  if (chIdx >= chapterPages_.size() || !lastText_) {
    return;
  }
  if (!chapterPages_[chIdx].paginated) {
    chapterPages_[chIdx].pages =
        paginateChapter(chIdx, *lastText_, layoutKey_.width, layoutKey_.height,
                        layoutKey_.bodySize, layoutKey_.lineSpacing);
    chapterPages_[chIdx].paginated = true;
  }
}

void Reader::restore(Anchor anchor) {
  if (anchor.first < book_.blocks.size()) {
    bookmark_ = anchor;
  }
}

void Reader::mark() {
  const Page *p = page();
  if (p) {
    bookmark_ = p->anchor;
  }
}

uint32_t Reader::permille() const {
  if (!chapterPages_.empty() && currentChapter_ + 1 == chapterPages_.size()) {
    const auto &cp = chapterPages_[currentChapter_];
    if (cp.paginated && !cp.pages.empty() &&
        currentPageInChapter_ + 1 == cp.pages.size() &&
        (chapterPages_.size() > 1 || cp.pages.size() > 1)) {
      return 1000;
    }
  }
  if (!bookmark_) {
    return 0;
  }
  std::size_t before =
      (bookmark_->first < cumulative_.size() ? cumulative_[bookmark_->first]
                                             : 0) +
      bookmark_->second;
  return static_cast<uint32_t>(
      std::min<std::size_t>(1000, (before * 1000) / totalBytes_));
}

void Reader::ensureLayout(TextRenderer &text, int width, int height,
                          int bodySize, int lineSpacing) {
  lastText_ = &text;
  LayoutKey key{width, height, bodySize, lineSpacing};
  if (layoutKey_ != key) {
    for (auto &cp : chapterPages_) {
      cp.paginated = false;
      cp.pages.clear();
    }
    layoutKey_ = key;
  }

  if (chapterPages_.empty()) {
    return;
  }

  currentChapter_ = bookmark_ ? chapterForBlock(bookmark_->first) : 0;
  ensureChapterPaginated(currentChapter_);

  const auto &pgs = chapterPages_[currentChapter_].pages;
  currentPageInChapter_ = 0;
  if (bookmark_ && !pgs.empty()) {
    for (std::size_t i = pgs.size(); i-- > 0;) {
      if (pgs[i].anchor <= *bookmark_) {
        currentPageInChapter_ = i;
        break;
      }
    }
  }
  if (!bookmark_) {
    mark();
  }
}

const Page *Reader::page() const {
  if (currentChapter_ < chapterPages_.size()) {
    const auto &pgs = chapterPages_[currentChapter_].pages;
    if (currentPageInChapter_ < pgs.size()) {
      return &pgs[currentPageInChapter_];
    }
  }
  return nullptr;
}

std::size_t Reader::activeChapterPageCount() const {
  if (currentChapter_ < chapterPages_.size()) {
    return chapterPages_[currentChapter_].pages.size();
  }
  return 0;
}

std::size_t Reader::paginatedChapterCount() const {
  std::size_t count = 0;
  for (const auto &cp : chapterPages_) {
    if (cp.paginated) {
      ++count;
    }
  }
  return count;
}

const std::vector<Page> &Reader::activeChapterPages() const {
  static const std::vector<Page> empty;
  if (currentChapter_ < chapterPages_.size()) {
    return chapterPages_[currentChapter_].pages;
  }
  return empty;
}

std::pair<std::size_t, std::size_t> Reader::pageNumber() const {
  std::size_t total = std::max<std::size_t>(1, activeChapterPageCount());
  return {currentPageInChapter_ + 1, total};
}

std::pair<std::size_t, std::size_t> Reader::chapterNumber() const {
  return {currentChapter_ + 1, std::max<std::size_t>(1, book_.chapters.size())};
}

void Reader::nextPage() {
  if (currentChapter_ >= chapterPages_.size()) {
    return;
  }
  ensureChapterPaginated(currentChapter_);
  const auto &pgs = chapterPages_[currentChapter_].pages;
  if (currentPageInChapter_ + 1 < pgs.size()) {
    ++currentPageInChapter_;
    mark();
  } else if (currentChapter_ + 1 < chapterPages_.size()) {
    ++currentChapter_;
    ensureChapterPaginated(currentChapter_);
    currentPageInChapter_ = 0;
    mark();
  }
}

void Reader::prevPage() {
  if (currentChapter_ >= chapterPages_.size()) {
    return;
  }
  ensureChapterPaginated(currentChapter_);
  if (currentPageInChapter_ > 0) {
    --currentPageInChapter_;
    mark();
  } else if (currentChapter_ > 0) {
    --currentChapter_;
    ensureChapterPaginated(currentChapter_);
    const auto &pgs = chapterPages_[currentChapter_].pages;
    currentPageInChapter_ = pgs.empty() ? 0 : (pgs.size() - 1);
    mark();
  }
}

void Reader::nextChapter() {
  if (currentChapter_ + 1 < chapterPages_.size()) {
    ++currentChapter_;
    ensureChapterPaginated(currentChapter_);
    currentPageInChapter_ = 0;
    mark();
  }
}

void Reader::prevChapter() {
  if (currentPageInChapter_ > 0) {
    currentPageInChapter_ = 0;
    mark();
  } else if (currentChapter_ > 0) {
    --currentChapter_;
    ensureChapterPaginated(currentChapter_);
    currentPageInChapter_ = 0;
    mark();
  }
}

void Reader::gotoChapter(std::size_t chIdx) {
  if (chIdx >= chapterPages_.size()) {
    return;
  }
  currentChapter_ = chIdx;
  ensureChapterPaginated(currentChapter_);
  currentPageInChapter_ = 0;
  if (const Page *p = page()) {
    bookmark_ = p->anchor;
  } else if (chIdx < book_.chapters.size() &&
             book_.chapters[chIdx].firstBlockIndex < book_.blocks.size()) {
    bookmark_ = Anchor{book_.chapters[chIdx].firstBlockIndex, 0};
  }
}

std::string Reader::chapterDisplayTitle(std::size_t chIdx) const {
  if (chIdx < book_.chapters.size() && !book_.chapters[chIdx].title.empty()) {
    return book_.chapters[chIdx].title;
  }
  return "Chapter " + std::to_string(chIdx + 1);
}

uint32_t Reader::chapterPercent(std::size_t chIdx) const {
  if (chIdx >= book_.chapters.size()) {
    return 0;
  }
  return static_cast<uint32_t>(std::min<std::size_t>(
      100, (book_.chapters[chIdx].byteOffset * 100) / totalBytes_));
}

} // namespace reader
