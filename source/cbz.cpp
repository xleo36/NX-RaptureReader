#include "cbz.hpp"
#include "epub.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#include <stb_image.h>

#include <webp/decode.h>

#include <algorithm>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <new>
#include <string>
namespace fs = std::filesystem;

namespace cbz {
namespace {

std::string toLowerAscii(std::string s) {
  for (char &c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

std::string trim(const std::string &s) {
  std::size_t start = 0;
  while (start < s.size() &&
         std::isspace(static_cast<unsigned char>(s[start]))) {
    ++start;
  }
  std::size_t end = s.size();
  while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
    --end;
  }
  return s.substr(start, end - start);
}

bool isDigit(char c) {
  return std::isdigit(static_cast<unsigned char>(c)) != 0;
}

bool isImageEntry(const std::string &rawName) {
  if (rawName.empty() || rawName.back() == '/' || rawName.back() == '\\') {
    return false;
  }
  std::string name = rawName;
  std::replace(name.begin(), name.end(), '\\', '/');

  // Skip macOS resource forks and hidden dot-entries in any path component
  std::size_t pos = 0;
  while (pos < name.size()) {
    std::size_t slash = name.find('/', pos);
    std::string seg = (slash == std::string::npos)
                          ? name.substr(pos)
                          : name.substr(pos, slash - pos);
    if (!seg.empty() && (seg == "__MACOSX" || seg[0] == '.')) {
      return false;
    }
    pos = (slash == std::string::npos) ? name.size() : (slash + 1);
  }

  auto dot = name.rfind('.');
  if (dot == std::string::npos) {
    return false;
  }
  std::string ext = toLowerAscii(name.substr(dot));
  return ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".bmp" ||
         ext == ".gif" || ext == ".webp";
}

std::vector<std::string> collectPages(const zip_util::ZipReader &zip) {
  std::vector<std::string> pages;
  for (const auto &entry : zip.entries()) {
    if (isImageEntry(entry)) {
      pages.push_back(entry);
    }
  }
  std::sort(pages.begin(), pages.end(), naturalLess);
  return pages;
}

std::optional<std::string> findXmlTag(const std::string &xmlLower,
                                      const std::string &xmlOrig,
                                      const std::string &tagLower) {
  std::string open = "<" + tagLower;
  std::size_t pos = xmlLower.find(open);
  while (pos != std::string::npos) {
    std::size_t after = pos + open.size();
    if (after < xmlLower.size() &&
        (xmlLower[after] == '>' ||
         std::isspace(static_cast<unsigned char>(xmlLower[after])))) {
      std::size_t gt = xmlLower.find('>', after);
      if (gt == std::string::npos) {
        return std::nullopt;
      }
      if (gt > pos && xmlLower[gt - 1] == '/') {
        pos = xmlLower.find(open, gt + 1);
        continue;
      }
      std::string close = "</" + tagLower + ">";
      std::size_t end = xmlLower.find(close, gt + 1);
      if (end == std::string::npos) {
        return std::nullopt;
      }
      return trim(epub::decodeEntities(xmlOrig.substr(gt + 1, end - (gt + 1))));
    }
    pos = xmlLower.find(open, after);
  }
  return std::nullopt;
}

std::string cleanedStem(const std::string &path) {
  std::string stem = fs::path(path).stem().string();
  std::replace(stem.begin(), stem.end(), '_', ' ');
  stem = trim(stem);
  return stem.empty() ? "Unknown Comic" : stem;
}

Metadata buildMetadata(const zip_util::ZipReader &zip, const std::string &path,
                       std::size_t pageCount) {
  Metadata m;
  m.pageCount = pageCount;

  std::string comicInfo;
  for (const auto &entry : zip.entries()) {
    std::string norm = entry;
    std::replace(norm.begin(), norm.end(), '\\', '/');
    auto slash = norm.rfind('/');
    std::string base =
        (slash == std::string::npos) ? norm : norm.substr(slash + 1);
    if (toLowerAscii(base) == "comicinfo.xml") {
      std::string ignoreErr;
      if (zip.readEntry(entry, comicInfo, ignoreErr)) {
        break;
      }
      comicInfo.clear();
    }
  }

  if (!comicInfo.empty()) {
    std::string lower = toLowerAscii(comicInfo);
    auto title = findXmlTag(lower, comicInfo, "title");
    auto series = findXmlTag(lower, comicInfo, "series");
    auto number = findXmlTag(lower, comicInfo, "number");
    auto writer = findXmlTag(lower, comicInfo, "writer");
    auto manga = findXmlTag(lower, comicInfo, "manga");

    if (title && !title->empty()) {
      m.title = *title;
    } else if (series && !series->empty()) {
      m.title = (number && !number->empty()) ? (*series + " #" + *number)
                                             : *series;
    }
    if (writer && !writer->empty()) {
      m.author = *writer;
    }
    if (manga) {
      std::string mv = toLowerAscii(*manga);
      if (mv == "yes" || mv == "yesandrighttoleft") {
        m.mangaRtl = true;
      } else if (mv == "no") {
        m.mangaRtl = false;
      }
    }
  }

  if (m.title.empty()) {
    m.title = cleanedStem(path);
  }
  return m;
}

} // namespace

const char *zoomLabel(ZoomMode mode) {
  switch (mode) {
  case ZoomMode::FitPage:
    return "Fit";
  case ZoomMode::FitWidth:
    return "FitW";
  case ZoomMode::Zoom150:
    return "1.5x";
  case ZoomMode::Zoom200:
    return "2.0x";
  }
  return "Fit";
}

const char *zoomDescription(ZoomMode mode) {
  switch (mode) {
  case ZoomMode::FitPage:
    return "Fit Page";
  case ZoomMode::FitWidth:
    return "Fit Width";
  case ZoomMode::Zoom150:
    return "Zoom 1.5x";
  case ZoomMode::Zoom200:
    return "Zoom 2.0x";
  }
  return "Fit Page";
}

ZoomMode stepZoom(ZoomMode mode, int dir) {
  int cur = static_cast<int>(mode);
  int next = std::clamp(cur + dir, 0, 3);
  return static_cast<ZoomMode>(next);
}

ZoomMode cycleZoom(ZoomMode mode) {
  return static_cast<ZoomMode>((static_cast<int>(mode) + 1) % 4);
}

bool naturalLess(const std::string &a, const std::string &b) {
  std::size_t ia = 0;
  std::size_t ib = 0;
  while (ia < a.size() && ib < b.size()) {
    if (isDigit(a[ia]) && isDigit(b[ib])) {
      std::size_t startA = ia;
      std::size_t startB = ib;
      while (ia < a.size() && a[ia] == '0') {
        ++ia;
      }
      while (ib < b.size() && b[ib] == '0') {
        ++ib;
      }
      std::size_t sigStartA = ia;
      std::size_t sigStartB = ib;
      while (ia < a.size() && isDigit(a[ia])) {
        ++ia;
      }
      while (ib < b.size() && isDigit(b[ib])) {
        ++ib;
      }
      std::size_t sigLenA = ia - sigStartA;
      std::size_t sigLenB = ib - sigStartB;
      if (sigLenA != sigLenB) {
        return sigLenA < sigLenB;
      }
      for (std::size_t k = 0; k < sigLenA; ++k) {
        if (a[sigStartA + k] != b[sigStartB + k]) {
          return a[sigStartA + k] < b[sigStartB + k];
        }
      }
      std::size_t zerosA = sigStartA - startA;
      std::size_t zerosB = sigStartB - startB;
      if (zerosA != zerosB) {
        return zerosA < zerosB;
      }
    } else {
      unsigned char ca = static_cast<unsigned char>(
          std::tolower(static_cast<unsigned char>(a[ia])));
      unsigned char cb = static_cast<unsigned char>(
          std::tolower(static_cast<unsigned char>(b[ib])));
      if (ca != cb) {
        return ca < cb;
      }
      ++ia;
      ++ib;
    }
  }
  if (ia != a.size() || ib != b.size()) {
    return ia == a.size();
  }
  return a < b;
}

namespace {

bool dimensionsAcceptable(int w, int h, std::string &err) {
  if (w <= 0 || h <= 0) {
    err = "Image has invalid dimensions";
    return false;
  }
  if (static_cast<uint64_t>(w) * static_cast<uint64_t>(h) > MAX_IMAGE_PIXELS) {
    err = "Image is too large (" + std::to_string(w) + "x" +
          std::to_string(h) + " px)";
    return false;
  }
  return true;
}

/// Copies decoder output into `out`, reporting allocation failure as an
/// error instead of letting std::bad_alloc escape.
bool copyPixels(const uint8_t *pixels, int w, int h, DecodedImage &out,
                std::string &err) {
  const std::size_t byteCount =
      static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4;
  try {
    out.rgba.assign(pixels, pixels + byteCount);
  } catch (const std::bad_alloc &) {
    out = DecodedImage{};
    err = "Out of memory while decoding image";
    return false;
  }
  out.width = w;
  out.height = h;
  return true;
}

} // namespace

bool decodeImageBytes(const uint8_t *data, std::size_t size, DecodedImage &out,
                      std::string &err) {
  out = DecodedImage{};
  if (!data || size == 0 || size > static_cast<std::size_t>(INT_MAX)) {
    err = "Invalid encoded image size";
    return false;
  }

  // WebP container signature: "RIFF" + 4-byte size + "WEBP"
  if (size >= 12 && std::memcmp(data, "RIFF", 4) == 0 &&
      std::memcmp(data + 8, "WEBP", 4) == 0) {
    int w = 0;
    int h = 0;
    // Read the header first so oversized images never reach the decoder.
    if (!WebPGetInfo(data, size, &w, &h)) {
      err = "WebP decode failed";
      return false;
    }
    if (!dimensionsAcceptable(w, h, err)) {
      return false;
    }
    uint8_t *pixels = WebPDecodeRGBA(data, size, &w, &h);
    if (!pixels || w <= 0 || h <= 0) {
      if (pixels) {
        WebPFree(pixels);
      }
      err = "WebP decode failed";
      return false;
    }
    bool ok = copyPixels(pixels, w, h, out, err);
    WebPFree(pixels);
    return ok;
  }

  int w = 0;
  int h = 0;
  int channels = 0;
  // Header-only probe: fails for unsupported/corrupt data, in which case the
  // full load below reports stb's failure reason.
  if (stbi_info_from_memory(data, static_cast<int>(size), &w, &h, &channels) &&
      !dimensionsAcceptable(w, h, err)) {
    return false;
  }
  stbi_uc *pixels =
      stbi_load_from_memory(data, static_cast<int>(size), &w, &h, &channels, 4);
  if (!pixels || w <= 0 || h <= 0) {
    if (pixels) {
      stbi_image_free(pixels);
    }
    const char *reason = stbi_failure_reason();
    err = reason ? std::string("Image decode failed: ") + reason
                 : "Image decode failed";
    return false;
  }

  bool ok = copyPixels(pixels, w, h, out, err);
  stbi_image_free(pixels);
  return ok;
}

DecodedImage downsampleRgba(const DecodedImage &src, int maxW, int maxH) {
  if (src.width <= 0 || src.height <= 0 || maxW <= 0 || maxH <= 0 ||
      src.rgba.size() <
          static_cast<std::size_t>(src.width) * src.height * 4) {
    return {};
  }
  if (src.width <= maxW && src.height <= maxH) {
    return src;
  }
  double scale =
      std::min(static_cast<double>(maxW) / static_cast<double>(src.width),
               static_cast<double>(maxH) / static_cast<double>(src.height));
  int dstW = std::clamp(
      static_cast<int>(std::lround(static_cast<double>(src.width) * scale)), 1,
      maxW);
  int dstH = std::clamp(
      static_cast<int>(std::lround(static_cast<double>(src.height) * scale)), 1,
      maxH);

  DecodedImage dst;
  dst.width = dstW;
  dst.height = dstH;
  dst.rgba.resize(static_cast<std::size_t>(dstW) * dstH * 4);

  for (int y = 0; y < dstH; ++y) {
    int y0 = (y * src.height) / dstH;
    int y1 = std::max(y0 + 1, ((y + 1) * src.height) / dstH);
    y1 = std::min(y1, src.height);
    for (int x = 0; x < dstW; ++x) {
      int x0 = (x * src.width) / dstW;
      int x1 = std::max(x0 + 1, ((x + 1) * src.width) / dstW);
      x1 = std::min(x1, src.width);

      uint64_t r = 0, g = 0, b = 0, a = 0;
      uint64_t count = 0;
      for (int sy = y0; sy < y1; ++sy) {
        const uint8_t *row =
            src.rgba.data() + static_cast<std::size_t>(sy) * src.width * 4;
        for (int sx = x0; sx < x1; ++sx) {
          const uint8_t *p = row + sx * 4;
          r += p[0];
          g += p[1];
          b += p[2];
          a += p[3];
          ++count;
        }
      }
      uint8_t *dp =
          dst.rgba.data() + (static_cast<std::size_t>(y) * dstW + x) * 4;
      dp[0] = static_cast<uint8_t>(r / count);
      dp[1] = static_cast<uint8_t>(g / count);
      dp[2] = static_cast<uint8_t>(b / count);
      dp[3] = static_cast<uint8_t>(a / count);
    }
  }
  return dst;
}

bool extractCoverImage(const std::string &path, std::vector<uint8_t> &out,
                       std::string &err) {
  zip_util::ZipReader zip;
  if (!zip.open(path, err)) {
    return false;
  }
  auto pages = collectPages(zip);
  if (pages.empty()) {
    err = "No image pages found in CBZ";
    return false;
  }
  return zip.readEntryBytes(pages[0], out, err);
}

SDL_Rect fitRect(int imgW, int imgH, SDL_Rect bounds) {
  float px = 0.0f;
  float py = 0.0f;
  return zoomedRect(imgW, imgH, bounds, ZoomMode::FitPage, px, py);
}

SDL_Rect zoomedRect(int imgW, int imgH, SDL_Rect bounds, ZoomMode mode,
                    float &panX, float &panY) {
  if (imgW <= 0 || imgH <= 0 || bounds.w <= 0 || bounds.h <= 0) {
    panX = 0.0f;
    panY = 0.0f;
    return SDL_Rect{bounds.x, bounds.y, 0, 0};
  }
  double fitScale =
      std::min(static_cast<double>(bounds.w) / static_cast<double>(imgW),
               static_cast<double>(bounds.h) / static_cast<double>(imgH));
  double scale = fitScale;
  switch (mode) {
  case ZoomMode::FitPage:
    scale = fitScale;
    break;
  case ZoomMode::FitWidth:
    scale = static_cast<double>(bounds.w) / static_cast<double>(imgW);
    break;
  case ZoomMode::Zoom150:
    scale = fitScale * 1.5;
    break;
  case ZoomMode::Zoom200:
    scale = fitScale * 2.0;
    break;
  }

  int dw = std::max(
      1, static_cast<int>(std::lround(static_cast<double>(imgW) * scale)));
  int dh = std::max(
      1, static_cast<int>(std::lround(static_cast<double>(imgH) * scale)));

  float maxPanX = std::max(0.0f, static_cast<float>(dw - bounds.w) * 0.5f);
  float maxPanY = std::max(0.0f, static_cast<float>(dh - bounds.h) * 0.5f);
  panX = std::clamp(panX, -maxPanX, maxPanX);
  panY = std::clamp(panY, -maxPanY, maxPanY);

  int dx = bounds.x + (bounds.w - dw) / 2 - static_cast<int>(std::lround(panX));
  int dy = bounds.y + (bounds.h - dh) / 2 - static_cast<int>(std::lround(panY));
  return SDL_Rect{dx, dy, dw, dh};
}

bool hasOverflow(int imgW, int imgH, SDL_Rect bounds, ZoomMode mode) {
  if (imgW <= 0 || imgH <= 0 || bounds.w <= 0 || bounds.h <= 0) {
    return false;
  }
  float px = 0.0f;
  float py = 0.0f;
  SDL_Rect r = zoomedRect(imgW, imgH, bounds, mode, px, py);
  return r.w > bounds.w || r.h > bounds.h;
}

std::optional<Metadata> metadata(const std::string &path, std::string &err) {
  zip_util::ZipReader zip;
  if (!zip.open(path, err)) {
    return std::nullopt;
  }
  auto pages = collectPages(zip);
  if (pages.empty()) {
    err = "No image pages found in CBZ";
    return std::nullopt;
  }
  return buildMetadata(zip, path, pages.size());
}

std::optional<ComicReader> ComicReader::open(const std::string &path,
                                             std::string &err) {
  ComicReader reader;
  if (!reader.zip_.open(path, err)) {
    return std::nullopt;
  }
  reader.pages_ = collectPages(reader.zip_);
  if (reader.pages_.empty()) {
    err = "No image pages found in CBZ";
    return std::nullopt;
  }
  reader.meta_ = buildMetadata(reader.zip_, path, reader.pages_.size());
  return reader;
}

void ComicReader::restore(std::pair<std::size_t, std::size_t> anchor) {
  if (anchor.first < pages_.size()) {
    currentPage_ = anchor.first;
  }
}

std::optional<std::pair<std::size_t, std::size_t>> ComicReader::anchor() const {
  if (pages_.empty()) {
    return std::nullopt;
  }
  return std::pair<std::size_t, std::size_t>{currentPage_, 0};
}

uint32_t ComicReader::permille() const {
  if (pages_.size() <= 1) {
    return 1000;
  }
  return static_cast<uint32_t>(
      std::min<std::size_t>(1000, (currentPage_ * 1000) / (pages_.size() - 1)));
}

void ComicReader::nextPage() {
  if (currentPage_ + 1 < pages_.size()) {
    ++currentPage_;
  }
}

void ComicReader::prevPage() {
  if (currentPage_ > 0) {
    --currentPage_;
  }
}

void ComicReader::jumpBy(int delta) {
  if (pages_.empty()) {
    return;
  }
  int64_t maxIdx = static_cast<int64_t>(pages_.size()) - 1;
  int64_t next = std::clamp<int64_t>(
      static_cast<int64_t>(currentPage_) + delta, 0, maxIdx);
  currentPage_ = static_cast<std::size_t>(next);
}

void ComicReader::gotoPage(std::size_t idx) {
  if (idx < pages_.size()) {
    currentPage_ = idx;
  }
}

std::string ComicReader::pageDisplayTitle(std::size_t idx) const {
  if (idx >= pages_.size()) {
    return "Page " + std::to_string(idx + 1);
  }
  std::string norm = pages_[idx];
  std::replace(norm.begin(), norm.end(), '\\', '/');
  auto slash = norm.rfind('/');
  std::string file =
      (slash == std::string::npos) ? norm : norm.substr(slash + 1);
  return "Page " + std::to_string(idx + 1) + "  (" + file + ")";
}

uint32_t ComicReader::pagePercent(std::size_t idx) const {
  if (pages_.size() <= 1 || idx >= pages_.size()) {
    return pages_.empty() ? 0 : 100;
  }
  return static_cast<uint32_t>(
      std::min<std::size_t>(100, (idx * 100) / (pages_.size() - 1)));
}

bool ComicReader::decodePage(std::size_t idx, DecodedImage &out,
                             std::string &err) const {
  out = DecodedImage{};
  if (idx >= pages_.size()) {
    err = "Page index out of range";
    return false;
  }
  try {
    std::vector<uint8_t> encoded;
    if (!zip_.readEntryBytes(pages_[idx], encoded, err)) {
      return false;
    }
    return decodeImageBytes(encoded.data(), encoded.size(), out, err);
  } catch (const std::bad_alloc &) {
    out = DecodedImage{};
    err = "Out of memory while decoding page";
    return false;
  }
}

} // namespace cbz
