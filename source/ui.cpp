#include "ui.hpp"

#include <algorithm>
#include <cmath>

namespace ui {
namespace {

void setColor(SDL_Renderer *r, SDL_Color c) {
  SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
}

/// Draws word-wrapped, horizontally centered text whose last line ends at `bottom`.
void drawWrappedBottom(SDL_Renderer *r, TextRenderer &text,
                       const std::string &s, int size, SDL_Color color,
                       int centerX, int bottom, int maxW) {
  auto [lines, lh] = text.wrapLines(s, size, maxW);
  int top = bottom - static_cast<int>(lines.size()) * lh;
  for (std::size_t i = 0; i < lines.size(); ++i) {
    text.drawTextCentered(r, lines[i].text, centerX,
                          top + static_cast<int>(i) * lh + lh / 2, size, color);
  }
}

} // namespace

int margin(int lw, int marginPercent) {
  return std::max(16, static_cast<int>(static_cast<float>(lw) *
                                       (static_cast<float>(marginPercent) /
                                        100.0f)));
}

SDL_Rect readerArea(int lw, int lh, int marginPercent) {
  int m = margin(lw, marginPercent);
  return SDL_Rect{m, HEADER_H, std::max(0, lw - 2 * m),
                  std::max(0, lh - (HEADER_H + FOOTER_H))};
}

std::string fitText(TextRenderer &text, const std::string &s, int size,
                    int maxW) {
  if (text.measureText(s, size).first <= maxW) {
    return s;
  }
  // Step backwards along UTF-8 codepoint boundaries
  std::size_t len = s.size();
  while (len > 0) {
    --len;
    while (len > 0 && (static_cast<uint8_t>(s[len]) & 0xC0u) == 0x80u) {
      --len;
    }
    if (len == 0) {
      break;
    }
    std::string candidate = s.substr(0, len) + "...";
    if (text.measureText(candidate, size).first <= maxW) {
      return candidate;
    }
  }
  return "...";
}

void drawReader(SDL_Renderer *r, TextRenderer &text, const Theme &theme,
                const reader::Reader &reader, int lw, int lh, int marginPercent,
                const std::string &sleepLabel,
                const std::string &brightnessLabel,
                const std::string &lineSpacingLabel,
                const std::string &marginLabel, bool showHud,
                const char *toast) {
  setColor(r, theme.bg);
  SDL_RenderClear(r);

  int hudM = margin(lw);
  int maxW = std::max(0, lw - 2 * hudM);

  if (showHud) {
    // Header: [< Lib] button, dimmed book title, and touch controls on the right
    text.drawText(r, "[< Lib]", hudM, 20, 16, theme.muted);

    int titleX = std::max(hudM + text.measureText("[< Lib]", 16).first + 16,
                          static_cast<int>(static_cast<float>(lw) * 0.16f));
    int titleMaxW = std::max(
        0, static_cast<int>(static_cast<float>(lw) * 0.45f) - titleX);
    if (titleMaxW > 40) {
      std::string header = reader.book().author.empty()
                               ? reader.book().title
                               : (reader.book().title + "  -  " +
                                  reader.book().author);
      header = fitText(text, header, 16, titleMaxW);
      text.drawText(r, header, titleX, 20, 16, theme.muted);
    }

    std::string sleepBtn = "[" + sleepLabel + "]";
    text.drawTextCentered(r, "[A-]",
                          static_cast<int>(static_cast<float>(lw) * 0.505f), 28,
                          16, theme.muted);
    text.drawTextCentered(r, "[A+]",
                          static_cast<int>(static_cast<float>(lw) * 0.595f), 28,
                          16, theme.muted);
    text.drawTextCentered(r, sleepBtn,
                          static_cast<int>(static_cast<float>(lw) * 0.690f), 28,
                          16, theme.muted);
    text.drawTextCentered(r, "[Theme]",
                          static_cast<int>(static_cast<float>(lw) * 0.805f), 28,
                          16, theme.muted);
    text.drawTextCentered(r, "[Flip]",
                          static_cast<int>(static_cast<float>(lw) * 0.935f), 28,
                          16, theme.muted);
  }

  // Page text directly on the background (no card), so on OLED only letters light up.
  // Chapter title lines only exist in `page->lines` on page 0 of a chapter.
  SDL_Rect area = readerArea(lw, lh, marginPercent);
  if (const reader::Page *page = reader.page()) {
    for (const auto &line : page->lines) {
      SDL_Color color = line.heading ? theme.heading : theme.text;
      text.drawText(r, line.text, area.x, area.y + line.y, line.size, color,
                    line.spans);
    }
  }

  // Footer: transient toast takes priority (even with HUD hidden); otherwise show
  // bottom-left typography buttons, progress, and bottom-right dimmer.
  int bottom = lh - 18;
  if (toast && toast[0] != '\0') {
    drawWrappedBottom(r, text, toast, 16, theme.text, lw / 2, bottom, maxW);
  } else if (showHud) {
    std::string spacingBtn = "[" + lineSpacingLabel + "]";
    std::string marginBtn = "[" + marginLabel + "]";
    text.drawTextCentered(r, spacingBtn,
                          static_cast<int>(static_cast<float>(lw) * 0.085f),
                          lh - 27, 16, theme.muted);
    text.drawTextCentered(r, marginBtn,
                          static_cast<int>(static_cast<float>(lw) * 0.205f),
                          lh - 27, 16, theme.muted);

    auto [page, pages] = reader.pageNumber();
    auto [chapter, chapters] = reader.chapterNumber();
    std::string footer = "[Ch " + std::to_string(chapter) + "/" +
                         std::to_string(chapters) + "]   Page " +
                         std::to_string(page) + " / " + std::to_string(pages) +
                         "   " + std::to_string(reader.permille() / 10) + "%";
    drawWrappedBottom(r, text, footer, 16, theme.muted, lw / 2, bottom, maxW);

    std::string dimBtn = "[" + brightnessLabel + "]";
    text.drawTextCentered(r, dimBtn,
                          static_cast<int>(static_cast<float>(lw) * 0.890f),
                          lh - 27, 16, theme.muted);
  }
}

void drawLibrary(SDL_Renderer *r, TextRenderer &text, const Theme &theme,
                 const std::vector<library::Entry> &entries,
                 std::size_t selected, int lw, int lh, const char *hint,
                 const std::string &emptyMessage,
                 const std::string &sleepLabel,
                 const std::string &brightnessLabel, const char *toast,
                 CoverCache *covers, int brightness) {
  setColor(r, theme.bg);
  SDL_RenderClear(r);

  int m = margin(lw);
  int maxW = std::max(0, lw - 2 * m);

  std::string dimBtn = "[" + brightnessLabel + "]";
  std::string sleepBtn = "[" + sleepLabel + "]";
  text.drawText(r, "Library", m, 28, 34, theme.heading);
  text.drawTextCentered(r, dimBtn,
                        static_cast<int>(static_cast<float>(lw) * 0.590f), 44,
                        16, theme.muted);
  text.drawTextCentered(r, sleepBtn,
                        static_cast<int>(static_cast<float>(lw) * 0.690f), 44,
                        16, theme.muted);
  text.drawTextCentered(r, "[Theme]",
                        static_cast<int>(static_cast<float>(lw) * 0.805f), 44,
                        16, theme.muted);
  text.drawTextCentered(r, "[Flip]",
                        static_cast<int>(static_cast<float>(lw) * 0.935f), 44,
                        16, theme.muted);

  if (entries.empty()) {
    auto [lines, lineH] = text.wrapLines(emptyMessage, 20, maxW);
    for (std::size_t i = 0; i < lines.size(); ++i) {
      text.drawText(r, lines[i].text, m,
                    LIB_LIST_TOP + static_cast<int>(i) * lineH, 20,
                    theme.muted);
    }
  } else {
    std::size_t visible = static_cast<std::size_t>(
        std::max(1, (lh - LIB_LIST_TOP - LIB_BOTTOM_RESERVED) / LIB_ROW_H));
    std::size_t first =
        (selected >= visible) ? (selected + 1 - visible) : 0;
    int textX = m + 84;
    int textW = std::max(0, maxW - 84);
    long modVal = std::lround(255.0 *
                              static_cast<double>(std::clamp(brightness, 30, 100)) /
                              100.0);
    Uint8 texMod = static_cast<Uint8>(std::clamp(modVal, 76L, 255L));

    for (std::size_t row = first;
         row < entries.size() && row < first + visible; ++row) {
      const auto &entry = entries[row];
      int y = LIB_LIST_TOP + static_cast<int>(row - first) * LIB_ROW_H;
      bool isSelected = (row == selected);
      SDL_Color mainColor = isSelected ? theme.text : theme.muted;

      if (isSelected) {
        text.drawText(r, ">", m, y + 20, 26, theme.text);
      }

      SDL_Rect coverSlot{m + 24, y + 6, 48, 68};
      int cw = 0;
      int ch = 0;
      SDL_Texture *coverTex =
          covers ? covers->get(r, entry, cw, ch) : nullptr;
      if (coverTex && cw > 0 && ch > 0) {
        SDL_Rect dst = cbz::fitRect(cw, ch, coverSlot);
        SDL_SetTextureColorMod(coverTex, texMod, texMod, texMod);
        SDL_RenderCopy(r, coverTex, nullptr, &dst);
      } else {
        const char *badge = "EPUB";
        if (entry.format == library::Format::Cbz) {
          badge = "CBZ";
        } else if (entry.format == library::Format::Pdf) {
          badge = "PDF";
        }
        text.drawTextCentered(r, badge, coverSlot.x + coverSlot.w / 2,
                              coverSlot.y + coverSlot.h / 2, 13, theme.muted);
      }

      std::string title = fitText(text, entry.title, 24, textW);
      text.drawText(r, title, textX, y + 8, 24, mainColor);

      std::string prog;
      if (!entry.permille) {
        prog = "New";
      } else if (*entry.permille >= 990) {
        prog = "Finished";
      } else {
        prog = std::to_string(*entry.permille / 10) + "%";
      }
      std::string sub =
          entry.author.empty() ? prog : (entry.author + "   -   " + prog);
      sub = fitText(text, sub, 16, textW);
      text.drawText(r, sub, textX, y + 42, 16, theme.muted);
    }
  }

  const char *message = (toast && toast[0] != '\0') ? toast : hint;
  SDL_Color color = (toast && toast[0] != '\0') ? theme.text : theme.muted;
  drawWrappedBottom(r, text, message ? message : "", 15, color, lw / 2, lh - 18,
                    maxW);
}

namespace {

template <typename TitleFn, typename PercentFn>
void drawListOverlay(SDL_Renderer *r, TextRenderer &text, const Theme &theme,
                     const char *headingWord, const char *emptyLabel,
                     const std::string &bookTitle, std::size_t count,
                     std::size_t selected, std::size_t scrollTop,
                     std::size_t activeIdx, TitleFn &&titleFn,
                     PercentFn &&percentFn, int lw, int lh, const char *hint,
                     const char *toast) {
  setColor(r, theme.bg);
  SDL_RenderClear(r);

  int m = margin(lw);
  int maxW = std::max(0, lw - 2 * m);

  std::size_t visible = tocVisibleRows(lh);
  std::size_t clampedSel = (count > 0) ? std::min(selected, count - 1) : 0;
  std::size_t first = clampTocTop(scrollTop, clampedSel, count, visible);

  // Header: [< Back], centered item count/range, and [Flip]
  text.drawText(r, "[< Back]", m, 20, 16, theme.muted);
  text.drawTextCentered(r, "[Flip]",
                        static_cast<int>(static_cast<float>(lw) * 0.935f), 28,
                        16, theme.muted);

  int leftEdge =
      std::max(m + text.measureText("[< Back]", 16).first + 16,
               static_cast<int>(static_cast<float>(lw) * 0.22f));
  int rightEdge = static_cast<int>(static_cast<float>(lw) * 0.85f);
  std::string headerTitle;
  if (count == 0) {
    headerTitle = headingWord;
  } else if (count > visible) {
    headerTitle = std::string(headingWord) + "  (" +
                  std::to_string(first + 1) + "-" +
                  std::to_string(std::min(count, first + visible)) + " / " +
                  std::to_string(count) + ")";
  } else {
    headerTitle =
        std::string(headingWord) + "  (" + std::to_string(count) + ")";
  }
  int headerMaxW = std::max(0, rightEdge - leftEdge);
  if (headerMaxW > 20) {
    headerTitle = fitText(text, headerTitle, 18, headerMaxW);
    text.drawTextCentered(r, headerTitle, (leftEdge + rightEdge) / 2, 28, 18,
                          theme.heading);
  }

  if (!bookTitle.empty() && maxW > 0) {
    std::string fitted = fitText(text, bookTitle, 16, maxW);
    text.drawText(r, fitted, m, 60, 16, theme.muted);
  }

  if (count == 0) {
    text.drawText(r, emptyLabel, m, TOC_LIST_TOP, 20, theme.muted);
  } else {
    int textX = m + 26;
    for (std::size_t row = first; row < count && row < first + visible;
         ++row) {
      int y = TOC_LIST_TOP + static_cast<int>(row - first) * TOC_ROW_H;
      bool isSelected = (row == clampedSel);
      bool isCurrent = (row == activeIdx);

      if (isSelected) {
        text.drawText(r, ">", m, y + 14, 22, theme.text);
      } else if (isCurrent) {
        text.drawText(r, "*", m + 2, y + 14, 22, theme.heading);
      }

      std::string pct = std::to_string(percentFn(row)) + "%";
      std::string rightLabel = isCurrent ? ("Now  " + pct) : pct;
      int pctW = text.measureText(rightLabel, 16).first;
      text.drawText(r, rightLabel, lw - m - pctW, y + 18, 16,
                    isCurrent ? theme.heading : theme.muted);

      int titleMaxW = std::max(0, (lw - m - pctW - 16) - textX);
      std::string title = fitText(text, titleFn(row), 22, titleMaxW);
      text.drawText(r, title, textX, y + 14, 22,
                    isSelected ? theme.text : theme.muted);
    }

    // Minimal 2px scrollbar thumb only (no background track on OLED)
    if (count > visible) {
      int thumbX = lw - std::max(6, m / 2);
      int trackH = static_cast<int>(visible) * TOC_ROW_H;
      int thumbH = std::max(
          24, static_cast<int>((visible * static_cast<std::size_t>(trackH)) /
                               count));
      std::size_t maxTop = count - visible;
      int thumbY =
          TOC_LIST_TOP +
          static_cast<int>((first * static_cast<std::size_t>(trackH - thumbH)) /
                           maxTop);
      setColor(r, theme.muted);
      SDL_Rect thumb{thumbX, thumbY, 2, thumbH};
      SDL_RenderFillRect(r, &thumb);
    }
  }

  const char *message = (toast && toast[0] != '\0') ? toast : hint;
  SDL_Color color = (toast && toast[0] != '\0') ? theme.text : theme.muted;
  drawWrappedBottom(r, text, message ? message : "", 15, color, lw / 2, lh - 18,
                    maxW);
}

} // namespace

namespace {

SDL_Texture *uploadRgbaTexture(SDL_Renderer *r, const cbz::DecodedImage &img,
                               std::string &err) {
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
  SDL_Texture *tex =
      SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
                        img.width, img.height);
  if (!tex) {
    err = std::string("Creating texture: ") + SDL_GetError();
    return nullptr;
  }
  if (SDL_UpdateTexture(tex, nullptr, img.rgba.data(), img.width * 4) != 0) {
    err = std::string("Uploading texture: ") + SDL_GetError();
    SDL_DestroyTexture(tex);
    return nullptr;
  }
  return tex;
}

} // namespace

SDL_Texture *CoverCache::get(SDL_Renderer *r, const library::Entry &entry,
                             int &outW, int &outH) {
  outW = 0;
  outH = 0;
  if (!r) {
    return nullptr;
  }
  if (owner != r) {
    clear();
    owner = r;
  }

  auto it = items.find(entry.path);
  if (it != items.end()) {
    outW = it->second.width;
    outH = it->second.height;
    return it->second.texture;
  }
  if (items.size() >= MAX_COVER_CACHE) {
    clear();
    owner = r;
  }
  auto &item = items[entry.path];
  item.attempted = true;

  cbz::DecodedImage thumb;
  std::string err;
  if (entry.format == library::Format::Cbz) {
    std::vector<uint8_t> raw;
    cbz::DecodedImage full;
    if (cbz::extractCoverImage(entry.path, raw, err) &&
        cbz::decodeImageBytes(raw.data(), raw.size(), full, err)) {
      thumb = cbz::downsampleRgba(full, 96, 136);
    }
  } else if (entry.format == library::Format::Pdf) {
    pdf::renderCover(entry.path, 96, 136, thumb, err);
  } else {
    std::vector<uint8_t> raw;
    cbz::DecodedImage full;
    if (epub::extractCoverImage(entry.path, raw, err) &&
        cbz::decodeImageBytes(raw.data(), raw.size(), full, err)) {
      thumb = cbz::downsampleRgba(full, 96, 136);
    }
  }

  if (thumb.width <= 0 || thumb.height <= 0 || thumb.rgba.empty()) {
    return nullptr;
  }
  SDL_Texture *tex = uploadRgbaTexture(r, thumb, err);
  if (!tex) {
    return nullptr;
  }
  item.texture = tex;
  item.width = thumb.width;
  item.height = thumb.height;
  outW = item.width;
  outH = item.height;
  return item.texture;
}

SDL_Texture *PageTextureCache::getComic(SDL_Renderer *r,
                                        const std::string &path,
                                        const cbz::ComicReader &comic,
                                        int &outW, int &outH,
                                        std::string &err) {
  outW = 0;
  outH = 0;
  if (!r) {
    err = "Renderer unavailable";
    return nullptr;
  }
  std::size_t idx = comic.currentPageIndex();
  if (valid && texture && owner == r && bookPath == path && pageIndex == idx &&
      reqW == 0 && reqH == 0) {
    outW = imgW;
    outH = imgH;
    return texture;
  }

  cbz::DecodedImage decoded;
  if (hasPrefetch && prefetchPath == path && prefetchIndex == idx &&
      prefetchReqW == 0 && prefetchReqH == 0) {
    decoded = std::move(prefetchImg);
    clearPrefetch();
  } else if (!comic.decodePage(idx, decoded, err)) {
    return nullptr;
  }

  SDL_Texture *tex = uploadRgbaTexture(r, decoded, err);
  if (!tex) {
    return nullptr;
  }

  if (texture) {
    SDL_DestroyTexture(texture);
  }
  texture = tex;
  owner = r;
  bookPath = path;
  pageIndex = idx;
  reqW = 0;
  reqH = 0;
  valid = true;
  imgW = decoded.width;
  imgH = decoded.height;
  outW = imgW;
  outH = imgH;
  return texture;
}

SDL_Texture *PageTextureCache::getPdf(SDL_Renderer *r, const std::string &path,
                                      const pdf::PdfReader &pdfDoc, int targetW,
                                      int targetH, int &outW, int &outH,
                                      std::string &err) {
  outW = 0;
  outH = 0;
  if (!r) {
    err = "Renderer unavailable";
    return nullptr;
  }
  std::size_t idx = pdfDoc.currentPageIndex();
  if (valid && texture && owner == r && bookPath == path && pageIndex == idx &&
      reqW == targetW && reqH == targetH) {
    outW = imgW;
    outH = imgH;
    return texture;
  }

  cbz::DecodedImage decoded;
  if (hasPrefetch && prefetchPath == path && prefetchIndex == idx &&
      prefetchReqW == targetW && prefetchReqH == targetH) {
    decoded = std::move(prefetchImg);
    clearPrefetch();
  } else if (!pdfDoc.renderPage(idx, targetW, targetH, decoded, err)) {
    return nullptr;
  }

  SDL_Texture *tex = uploadRgbaTexture(r, decoded, err);
  if (!tex) {
    return nullptr;
  }

  if (texture) {
    SDL_DestroyTexture(texture);
  }
  texture = tex;
  owner = r;
  bookPath = path;
  pageIndex = idx;
  reqW = targetW;
  reqH = targetH;
  valid = true;
  imgW = decoded.width;
  imgH = decoded.height;
  outW = imgW;
  outH = imgH;
  return texture;
}

void PageTextureCache::prefetchComic(const std::string &path,
                                     std::size_t nextIdx,
                                     const cbz::ComicReader &comic) {
  if (nextIdx >= comic.pageCount()) {
    return;
  }
  if (hasPrefetch && prefetchPath == path && prefetchIndex == nextIdx &&
      prefetchReqW == 0 && prefetchReqH == 0) {
    return;
  }
  cbz::DecodedImage decoded;
  std::string err;
  if (comic.decodePage(nextIdx, decoded, err)) {
    hasPrefetch = true;
    prefetchPath = path;
    prefetchIndex = nextIdx;
    prefetchReqW = 0;
    prefetchReqH = 0;
    prefetchImg = std::move(decoded);
  }
}

void PageTextureCache::prefetchPdf(const std::string &path, std::size_t nextIdx,
                                   const pdf::PdfReader &pdfDoc, int targetW,
                                   int targetH) {
  if (nextIdx >= pdfDoc.pageCount() || targetW <= 0 || targetH <= 0) {
    return;
  }
  if (hasPrefetch && prefetchPath == path && prefetchIndex == nextIdx &&
      prefetchReqW == targetW && prefetchReqH == targetH) {
    return;
  }
  cbz::DecodedImage decoded;
  std::string err;
  if (pdfDoc.renderPage(nextIdx, targetW, targetH, decoded, err)) {
    hasPrefetch = true;
    prefetchPath = path;
    prefetchIndex = nextIdx;
    prefetchReqW = targetW;
    prefetchReqH = targetH;
    prefetchImg = std::move(decoded);
  }
}

void drawToc(SDL_Renderer *r, TextRenderer &text, const Theme &theme,
             const reader::Reader &reader, std::size_t selected,
             std::size_t scrollTop, int lw, int lh, const char *hint,
             const char *toast) {
  drawListOverlay(
      r, text, theme, "Chapters", "No chapters found.", reader.book().title,
      reader.chapterCount(), selected, scrollTop, reader.currentChapterIndex(),
      [&](std::size_t idx) { return reader.chapterDisplayTitle(idx); },
      [&](std::size_t idx) { return reader.chapterPercent(idx); }, lw, lh, hint,
      toast);
}

void drawVisualPage(SDL_Renderer *r, TextRenderer &text, const Theme &theme,
                    const std::string &title, const std::string &author,
                    std::size_t pageIdx, std::size_t pageCount,
                    uint32_t permille, SDL_Texture *pageTex, int imgW,
                    int imgH, int lw, int lh, int brightness,
                    cbz::ZoomMode zoomMode, float &panX, float &panY,
                    const std::string *dirLabel, const std::string &sleepLabel,
                    const std::string &brightnessLabel, bool showHud,
                    const char *toast, const char *decodeError) {
  setColor(r, theme.bg);
  SDL_RenderClear(r);

  int hudM = margin(lw);
  int maxW = std::max(0, lw - 2 * hudM);

  SDL_Rect bounds =
      showHud ? SDL_Rect{0, HEADER_H, lw, std::max(0, lh - HEADER_H - FOOTER_H)}
              : SDL_Rect{0, 0, lw, lh};
  if (pageTex && imgW > 0 && imgH > 0) {
    SDL_Rect dst = cbz::zoomedRect(imgW, imgH, bounds, zoomMode, panX, panY);
    long modVal = std::lround(255.0 *
                              static_cast<double>(std::clamp(brightness, 30, 100)) /
                              100.0);
    Uint8 mod = static_cast<Uint8>(std::clamp(modVal, 76L, 255L));
    SDL_SetTextureColorMod(pageTex, mod, mod, mod);
    SDL_RenderSetClipRect(r, &bounds);
    SDL_RenderCopy(r, pageTex, nullptr, &dst);
    SDL_RenderSetClipRect(r, nullptr);
  } else if (decodeError && decodeError[0] != '\0') {
    auto [errLines, errLh] = text.wrapLines(decodeError, 18, maxW);
    int top = bounds.y +
              std::max(0, bounds.h - static_cast<int>(errLines.size()) * errLh) /
                  2;
    for (std::size_t i = 0; i < errLines.size(); ++i) {
      text.drawTextCentered(r, errLines[i].text, lw / 2,
                            top + static_cast<int>(i) * errLh + errLh / 2, 18,
                            theme.muted);
    }
  }

  std::string zoomBtn = std::string("[") + cbz::zoomLabel(zoomMode) + "]";

  if (showHud) {
    text.drawText(r, "[< Lib]", hudM, 20, 16, theme.muted);
    int titleX = std::max(hudM + text.measureText("[< Lib]", 16).first + 16,
                          static_cast<int>(static_cast<float>(lw) * 0.16f));
    int titleMaxW = std::max(
        0, static_cast<int>(static_cast<float>(lw) * 0.45f) - titleX);
    if (titleMaxW > 40) {
      std::string header =
          author.empty() ? title : (title + "  -  " + author);
      header = fitText(text, header, 16, titleMaxW);
      text.drawText(r, header, titleX, 20, 16, theme.muted);
    }

    std::string topMidBtn = dirLabel ? ("[" + *dirLabel + "]") : zoomBtn;
    std::string sleepBtn = "[" + sleepLabel + "]";
    text.drawTextCentered(r, topMidBtn,
                          static_cast<int>(static_cast<float>(lw) * 0.550f), 28,
                          16, theme.muted);
    text.drawTextCentered(r, sleepBtn,
                          static_cast<int>(static_cast<float>(lw) * 0.690f), 28,
                          16, theme.muted);
    text.drawTextCentered(r, "[Theme]",
                          static_cast<int>(static_cast<float>(lw) * 0.805f), 28,
                          16, theme.muted);
    text.drawTextCentered(r, "[Flip]",
                          static_cast<int>(static_cast<float>(lw) * 0.935f), 28,
                          16, theme.muted);
  }

  int bottom = lh - 18;
  if (toast && toast[0] != '\0') {
    drawWrappedBottom(r, text, toast, 16, theme.text, lw / 2, bottom, maxW);
  } else if (showHud) {
    if (dirLabel) {
      std::string dirBtn = "[" + *dirLabel + "]";
      text.drawTextCentered(r, dirBtn,
                            static_cast<int>(static_cast<float>(lw) * 0.085f),
                            lh - 27, 16, theme.muted);
      text.drawTextCentered(r, zoomBtn,
                            static_cast<int>(static_cast<float>(lw) * 0.205f),
                            lh - 27, 16, theme.muted);
    } else {
      text.drawTextCentered(r, zoomBtn,
                            static_cast<int>(static_cast<float>(lw) * 0.140f),
                            lh - 27, 16, theme.muted);
    }

    std::string footer = "[Page " + std::to_string(pageIdx + 1) + " / " +
                         std::to_string(pageCount) + "]   " +
                         std::to_string(permille / 10) + "%";
    drawWrappedBottom(r, text, footer, 16, theme.muted, lw / 2, bottom, maxW);

    std::string dimBtn = "[" + brightnessLabel + "]";
    text.drawTextCentered(r, dimBtn,
                          static_cast<int>(static_cast<float>(lw) * 0.890f),
                          lh - 27, 16, theme.muted);
  }
}

void drawComic(SDL_Renderer *r, TextRenderer &text, const Theme &theme,
               const cbz::ComicReader &comic, SDL_Texture *pageTex,
               int imgW, int imgH, int lw, int lh, int brightness,
               const std::string &dirLabel, const std::string &sleepLabel,
               const std::string &brightnessLabel, bool showHud,
               const char *toast, const char *decodeError) {
  float panX = 0.0f;
  float panY = 0.0f;
  drawVisualPage(r, text, theme, comic.title(), comic.author(),
                 comic.currentPageIndex(), comic.pageCount(), comic.permille(),
                 pageTex, imgW, imgH, lw, lh, brightness, cbz::ZoomMode::FitPage,
                 panX, panY, &dirLabel, sleepLabel, brightnessLabel, showHud,
                 toast, decodeError);
}

void drawComicPages(SDL_Renderer *r, TextRenderer &text, const Theme &theme,
                    const cbz::ComicReader &comic, std::size_t selected,
                    std::size_t scrollTop, int lw, int lh, const char *hint,
                    const char *toast) {
  drawListOverlay(
      r, text, theme, "Pages", "No pages found.", comic.title(),
      comic.pageCount(), selected, scrollTop, comic.currentPageIndex(),
      [&](std::size_t idx) { return comic.pageDisplayTitle(idx); },
      [&](std::size_t idx) { return comic.pagePercent(idx); }, lw, lh, hint,
      toast);
}

void drawPdfPages(SDL_Renderer *r, TextRenderer &text, const Theme &theme,
                  const pdf::PdfReader &pdfDoc, std::size_t selected,
                  std::size_t scrollTop, int lw, int lh, const char *hint,
                  const char *toast) {
  drawListOverlay(
      r, text, theme, "Pages", "No pages found.", pdfDoc.title(),
      pdfDoc.pageCount(), selected, scrollTop, pdfDoc.currentPageIndex(),
      [&](std::size_t idx) { return pdfDoc.pageDisplayTitle(idx); },
      [&](std::size_t idx) { return pdfDoc.pagePercent(idx); }, lw, lh, hint,
      toast);
}

} // namespace ui
