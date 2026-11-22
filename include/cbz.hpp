#pragma once

#include "zip_reader.hpp"
#include <SDL.h>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cbz {

struct Metadata {
  std::string title;
  std::string author;
  std::size_t pageCount = 0;
  std::optional<bool> mangaRtl;
};

struct DecodedImage {
  int width = 0;
  int height = 0;
  std::vector<uint8_t> rgba;
};

enum class ZoomMode : uint8_t {
  FitPage = 0,
  FitWidth = 1,
  Zoom150 = 2,
  Zoom200 = 3,
};

const char *zoomLabel(ZoomMode mode);
const char *zoomDescription(ZoomMode mode);
ZoomMode stepZoom(ZoomMode mode, int dir);
ZoomMode cycleZoom(ZoomMode mode);

/// Case-insensitive natural numeric comparison (`page_2.jpg < page_10.jpg`).
bool naturalLess(const std::string &a, const std::string &b);

/// Largest decoded page (width x height) accepted, ~160 MB of RGBA. Images
/// above it are rejected from their header alone, before any pixel buffer is
/// allocated, so a tiny hostile file cannot exhaust a console's RAM.
inline constexpr uint64_t MAX_IMAGE_PIXELS = 40'000'000;

/// Decodes JPEG/PNG/BMP/GIF (via stb_image) or WebP (via libwebp) to RGBA32.
/// Fails (without throwing) on corrupt, oversized, or out-of-memory input.
bool decodeImageBytes(const uint8_t *data, std::size_t size, DecodedImage &out,
                      std::string &err);

/// Downscales an RGBA32 image cleanly to fit within `maxW x maxH`.
DecodedImage downsampleRgba(const DecodedImage &src, int maxW, int maxH);

/// Extracts raw bytes of the first image page in a CBZ for Library cover art.
bool extractCoverImage(const std::string &path, std::vector<uint8_t> &out,
                       std::string &err);

/// Aspect-ratio-preserving centered fit inside `bounds`.
SDL_Rect fitRect(int imgW, int imgH, SDL_Rect bounds);

/// Computes destination rect for `mode`, clamping `panX`/`panY` to overflow bounds.
SDL_Rect zoomedRect(int imgW, int imgH, SDL_Rect bounds, ZoomMode mode,
                    float &panX, float &panY);

/// Returns true only when `mode` produces an image larger than `bounds`.
bool hasOverflow(int imgW, int imgH, SDL_Rect bounds, ZoomMode mode);

/// Reads CBZ metadata (`ComicInfo.xml` or filename stem) and page count cleanly.
std::optional<Metadata> metadata(const std::string &path, std::string &err);

class ComicReader {
public:
  static std::optional<ComicReader> open(const std::string &path,
                                         std::string &err);

  void restore(std::pair<std::size_t, std::size_t> anchor);
  std::optional<std::pair<std::size_t, std::size_t>> anchor() const;
  uint32_t permille() const;

  void nextPage();
  void prevPage();
  void jumpBy(int delta);
  void gotoPage(std::size_t idx);

  std::size_t currentPageIndex() const { return currentPage_; }
  std::size_t pageCount() const { return pages_.size(); }
  const std::string &title() const { return meta_.title; }
  const std::string &author() const { return meta_.author; }
  std::optional<bool> mangaRtlHint() const { return meta_.mangaRtl; }
  std::string pageDisplayTitle(std::size_t idx) const;
  uint32_t pagePercent(std::size_t idx) const;

  bool decodePage(std::size_t idx, DecodedImage &out, std::string &err) const;

private:
  zip_util::ZipReader zip_;
  Metadata meta_;
  std::vector<std::string> pages_;
  std::size_t currentPage_ = 0;
};

} // namespace cbz
