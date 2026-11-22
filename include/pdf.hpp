#pragma once

#include "cbz.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace pdf {

struct Metadata {
  std::string title;
  std::string author;
  std::size_t pageCount = 0;
};

/// Returns true when a PDF rendering backend is compiled in (MuPDF or CoreGraphics).
bool isAvailable();

/// Reads PDF title, author, and page count quickly.
std::optional<Metadata> metadata(const std::string &path, std::string &err);

/// Renders page 0 as a scaled-down RGBA32 cover thumbnail (`maxW x maxH`).
bool renderCover(const std::string &path, int maxW, int maxH,
                 cbz::DecodedImage &out, std::string &err);

class PdfReader {
public:
  struct Impl;
  static std::optional<PdfReader> open(const std::string &path,
                                         std::string &err);

  void restore(std::pair<std::size_t, std::size_t> anchor);
  std::optional<std::pair<std::size_t, std::size_t>> anchor() const;
  uint32_t permille() const;

  void nextPage();
  void prevPage();
  void jumpBy(int delta);
  void gotoPage(std::size_t idx);

  std::size_t currentPageIndex() const { return currentPage_; }
  std::size_t pageCount() const { return meta_.pageCount; }
  const std::string &title() const { return meta_.title; }
  const std::string &author() const { return meta_.author; }
  std::string pageDisplayTitle(std::size_t idx) const;
  uint32_t pagePercent(std::size_t idx) const;

  bool renderPage(std::size_t idx, int targetW, int targetH,
                  cbz::DecodedImage &out, std::string &err) const;

private:
  std::shared_ptr<Impl> impl_;
  Metadata meta_;
  std::size_t currentPage_ = 0;
};

} // namespace pdf
