#pragma once

#include "cbz.hpp"
#include "library.hpp"
#include "pdf.hpp"
#include "reader.hpp"
#include "text_renderer.hpp"
#include "theme.hpp"
#include <SDL.h>
#include <algorithm>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace ui {

inline constexpr int HEADER_H = 60;
inline constexpr int FOOTER_H = 56;
inline constexpr int LIB_LIST_TOP = 110;
inline constexpr int LIB_ROW_H = 84;
inline constexpr int LIB_BOTTOM_RESERVED = 90;

inline constexpr int TOC_LIST_TOP = 96;
inline constexpr int TOC_ROW_H = 56;
inline constexpr int TOC_BOTTOM_RESERVED = 72;

inline std::size_t tocVisibleRows(int lh) {
  return static_cast<std::size_t>(
      std::max(1, (lh - TOC_LIST_TOP - TOC_BOTTOM_RESERVED) / TOC_ROW_H));
}

inline std::size_t clampTocTop(std::size_t top, std::size_t selected,
                               std::size_t count, std::size_t visible) {
  if (count == 0 || count <= visible) {
    return 0;
  }
  selected = std::min(selected, count - 1);
  std::size_t maxTop = count - visible;
  top = std::min(top, maxTop);
  if (selected < top) {
    top = selected;
  } else if (selected >= top + visible) {
    top = selected + 1 - visible;
  }
  return std::min(top, maxTop);
}

int margin(int lw, int marginPercent = 7);
SDL_Rect readerArea(int lw, int lh, int marginPercent = 7);
std::string fitText(TextRenderer &text, const std::string &s, int size,
                    int maxW);

void drawReader(SDL_Renderer *r, TextRenderer &text, const Theme &theme,
                const reader::Reader &reader, int lw, int lh, int marginPercent,
                const std::string &sleepLabel,
                const std::string &brightnessLabel,
                const std::string &lineSpacingLabel,
                const std::string &marginLabel, bool showHud,
                const char *toast);

struct CoverCache {
  static constexpr std::size_t MAX_COVER_CACHE = 64;

  struct Item {
    SDL_Texture *texture = nullptr;
    int width = 0;
    int height = 0;
    bool attempted = false;
  };
  SDL_Renderer *owner = nullptr;
  std::unordered_map<std::string, Item> items;

  CoverCache() = default;
  ~CoverCache() { clear(); }
  CoverCache(const CoverCache &) = delete;
  CoverCache &operator=(const CoverCache &) = delete;

  void clear() {
    for (auto &kv : items) {
      if (kv.second.texture) {
        SDL_DestroyTexture(kv.second.texture);
      }
    }
    items.clear();
    owner = nullptr;
  }

  SDL_Texture *get(SDL_Renderer *r, const library::Entry &entry, int &outW,
                   int &outH);
};

struct PageTextureCache {
  SDL_Texture *texture = nullptr;
  SDL_Renderer *owner = nullptr;
  std::string bookPath;
  std::size_t pageIndex = 0;
  int reqW = 0;
  int reqH = 0;
  bool valid = false;
  int imgW = 0;
  int imgH = 0;

  // 1-page-ahead decoded RAM cache for instant page turns
  bool hasPrefetch = false;
  std::string prefetchPath;
  std::size_t prefetchIndex = 0;
  int prefetchReqW = 0;
  int prefetchReqH = 0;
  cbz::DecodedImage prefetchImg;

  PageTextureCache() = default;
  ~PageTextureCache() { clear(); }
  PageTextureCache(const PageTextureCache &) = delete;
  PageTextureCache &operator=(const PageTextureCache &) = delete;

  void clear() {
    if (texture) {
      SDL_DestroyTexture(texture);
      texture = nullptr;
    }
    owner = nullptr;
    bookPath.clear();
    pageIndex = 0;
    reqW = 0;
    reqH = 0;
    valid = false;
    imgW = 0;
    imgH = 0;
    clearPrefetch();
  }

  void clearPrefetch() {
    hasPrefetch = false;
    prefetchPath.clear();
    prefetchIndex = 0;
    prefetchReqW = 0;
    prefetchReqH = 0;
    prefetchImg = cbz::DecodedImage{};
  }

  SDL_Texture *getComic(SDL_Renderer *r, const std::string &path,
                        const cbz::ComicReader &comic, int &outW, int &outH,
                        std::string &err);

  SDL_Texture *getPdf(SDL_Renderer *r, const std::string &path,
                      const pdf::PdfReader &pdfDoc, int targetW, int targetH,
                      int &outW, int &outH, std::string &err);

  void prefetchComic(const std::string &path, std::size_t nextIdx,
                     const cbz::ComicReader &comic);

  void prefetchPdf(const std::string &path, std::size_t nextIdx,
                   const pdf::PdfReader &pdfDoc, int targetW, int targetH);

  // Backwards-compatible alias for unit tests
  SDL_Texture *get(SDL_Renderer *r, const std::string &path,
                   const cbz::ComicReader &comic, int &outW, int &outH,
                   std::string &err) {
    return getComic(r, path, comic, outW, outH, err);
  }
};

using ComicTextureCache = PageTextureCache;

void drawLibrary(SDL_Renderer *r, TextRenderer &text, const Theme &theme,
                 const std::vector<library::Entry> &entries,
                 std::size_t selected, int lw, int lh, const char *hint,
                 const std::string &emptyMessage,
                 const std::string &sleepLabel,
                 const std::string &brightnessLabel, const char *toast,
                 CoverCache *covers = nullptr, int brightness = 100);

void drawToc(SDL_Renderer *r, TextRenderer &text, const Theme &theme,
             const reader::Reader &reader, std::size_t selected,
             std::size_t scrollTop, int lw, int lh, const char *hint,
             const char *toast);

void drawVisualPage(SDL_Renderer *r, TextRenderer &text, const Theme &theme,
                    const std::string &title, const std::string &author,
                    std::size_t pageIdx, std::size_t pageCount,
                    uint32_t permille, SDL_Texture *pageTex, int imgW,
                    int imgH, int lw, int lh, int brightness,
                    cbz::ZoomMode zoomMode, float &panX, float &panY,
                    const std::string *dirLabel, const std::string &sleepLabel,
                    const std::string &brightnessLabel, bool showHud,
                    const char *toast, const char *decodeError = nullptr);

void drawComic(SDL_Renderer *r, TextRenderer &text, const Theme &theme,
               const cbz::ComicReader &comic, SDL_Texture *pageTex,
               int imgW, int imgH, int lw, int lh, int brightness,
               const std::string &dirLabel, const std::string &sleepLabel,
               const std::string &brightnessLabel, bool showHud,
               const char *toast, const char *decodeError = nullptr);

void drawComicPages(SDL_Renderer *r, TextRenderer &text, const Theme &theme,
                    const cbz::ComicReader &comic, std::size_t selected,
                    std::size_t scrollTop, int lw, int lh, const char *hint,
                    const char *toast);

void drawPdfPages(SDL_Renderer *r, TextRenderer &text, const Theme &theme,
                  const pdf::PdfReader &pdfDoc, std::size_t selected,
                  std::size_t scrollTop, int lw, int lh, const char *hint,
                  const char *toast);

} // namespace ui
