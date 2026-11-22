#include "text_renderer.hpp"

#include <algorithm>
#include <cmath>

namespace {

struct Utf8Char {
  uint32_t cp = 0;
  std::size_t len = 1;
};

Utf8Char decodeUtf8(const std::string &s, std::size_t i) {
  auto b0 = static_cast<uint8_t>(s[i]);
  if (b0 < 0x80u) {
    return {b0, 1};
  }
  if ((b0 & 0xE0u) == 0xC0u && i + 1 < s.size()) {
    uint32_t cp = ((b0 & 0x1Fu) << 6) | (static_cast<uint8_t>(s[i + 1]) & 0x3Fu);
    return {cp, 2};
  }
  if ((b0 & 0xF0u) == 0xE0u && i + 2 < s.size()) {
    uint32_t cp = ((b0 & 0x0Fu) << 12) |
                  ((static_cast<uint8_t>(s[i + 1]) & 0x3Fu) << 6) |
                  (static_cast<uint8_t>(s[i + 2]) & 0x3Fu);
    return {cp, 3};
  }
  if ((b0 & 0xF8u) == 0xF0u && i + 3 < s.size()) {
    uint32_t cp = ((b0 & 0x07u) << 18) |
                  ((static_cast<uint8_t>(s[i + 1]) & 0x3Fu) << 12) |
                  ((static_cast<uint8_t>(s[i + 2]) & 0x3Fu) << 6) |
                  (static_cast<uint8_t>(s[i + 3]) & 0x3Fu);
    return {cp, 4};
  }
  return {0xFFFDu, 1};
}

bool isControlOrSpace(uint32_t cp) {
  return cp < 0x20u || cp == 0x7Fu || cp == ' ' || cp == 0xA0u;
}

uint64_t glyphKey(uint32_t cp, int fontSize, uint8_t style) {
  uint64_t sBits = static_cast<uint64_t>(style & 0x3u);
  uint64_t sizeBits = static_cast<uint64_t>(static_cast<uint32_t>(fontSize) &
                                            0x3FFFFFFFu);
  return (sBits << 62) | (sizeBits << 32) | cp;
}

uint8_t styleAt(const std::vector<epub::StyleSpan> &spans, std::size_t offset,
                std::size_t &hintIdx) {
  while (hintIdx < spans.size() && spans[hintIdx].end <= offset) {
    ++hintIdx;
  }
  if (hintIdx < spans.size() && spans[hintIdx].start <= offset &&
      offset < spans[hintIdx].end) {
    return spans[hintIdx].style;
  }
  return epub::StyleRegular;
}

std::vector<epub::StyleSpan>
sliceSpans(const std::vector<epub::StyleSpan> &spans, std::size_t lineStart,
           std::size_t lineEnd) {
  std::vector<epub::StyleSpan> out;
  for (const auto &sp : spans) {
    if (sp.end <= lineStart) {
      continue;
    }
    if (sp.start >= lineEnd) {
      break;
    }
    std::size_t s = std::max(sp.start, lineStart) - lineStart;
    std::size_t e = std::min(sp.end, lineEnd) - lineStart;
    if (s < e) {
      out.push_back(epub::StyleSpan{s, e, sp.style});
    }
  }
  return out;
}

} // namespace

TextRenderer::~TextRenderer() { cleanup(); }

void TextRenderer::invalidateTextures() {
  for (auto &kv : cache_) {
    if (kv.second.texture) {
      SDL_DestroyTexture(kv.second.texture);
    }
  }
  cache_.clear();
}

void TextRenderer::cleanup() {
  invalidateTextures();
  if (italicFace_) {
    FT_Done_Face(italicFace_);
    italicFace_ = nullptr;
  }
  if (face_) {
    FT_Done_Face(face_);
    face_ = nullptr;
  }
  if (ft_) {
    FT_Done_FreeType(ft_);
    ft_ = nullptr;
  }
  renderer_ = nullptr;
  currentRegularSize_ = 0;
  currentItalicSize_ = 0;
}

std::optional<std::string> TextRenderer::init(const std::string &fontPath,
                                              SDL_Renderer *renderer,
                                              const std::string &italicFontPath) {
  cleanup();
  renderer_ = renderer;
  if (FT_Init_FreeType(&ft_) != 0) {
    return std::string("Failed to initialize FreeType");
  }
  if (FT_New_Face(ft_, fontPath.c_str(), 0, &face_) != 0) {
    cleanup();
    return std::string("Failed to load font: ") + fontPath;
  }
  if (!italicFontPath.empty()) {
    // Optional italic face: if missing, fall back cleanly to FT_GlyphSlot_Oblique.
    if (FT_New_Face(ft_, italicFontPath.c_str(), 0, &italicFace_) != 0) {
      italicFace_ = nullptr;
    }
  }
  return std::nullopt;
}

TextRenderer::FontMetrics TextRenderer::setSize(FT_Face face, int fontSize) {
  fontSize = std::max(1, fontSize);
  int &cachedSize =
      (face == italicFace_) ? currentItalicSize_ : currentRegularSize_;
  if (cachedSize != fontSize) {
    FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(fontSize));
    cachedSize = fontSize;
  }
  int ascent = static_cast<int>(face->size->metrics.ascender >> 6);
  int lineHeight = static_cast<int>(face->size->metrics.height >> 6);
  if (lineHeight <= 0) {
    lineHeight =
        static_cast<int>(std::ceil(static_cast<float>(fontSize) * 1.4f));
  }
  if (ascent <= 0) {
    ascent = fontSize;
  }
  return {ascent, lineHeight};
}

const TextRenderer::CachedGlyph *TextRenderer::getGlyph(uint32_t codepoint,
                                                        int fontSize,
                                                        uint8_t style,
                                                        bool needTexture) {
  style &= (epub::StyleItalic | epub::StyleBold);
  uint64_t key = glyphKey(codepoint, fontSize, style);
  auto it = cache_.find(key);
  if (it != cache_.end()) {
    if (!needTexture || it->second.rasterized) {
      return &it->second;
    }
  }

  bool wantItalic = (style & epub::StyleItalic) != 0;
  bool wantBold = (style & epub::StyleBold) != 0;
  FT_Face activeFace = (wantItalic && italicFace_) ? italicFace_ : face_;
  bool syntheticOblique = (wantItalic && !italicFace_);

  setSize(activeFace, fontSize);
  FT_UInt glyphIndex = FT_Get_Char_Index(activeFace, codepoint);
  if (FT_Load_Glyph(activeFace, glyphIndex, FT_LOAD_DEFAULT) != 0) {
    return nullptr;
  }

  FT_GlyphSlot slot = activeFace->glyph;
  if (syntheticOblique && !isControlOrSpace(codepoint)) {
    FT_GlyphSlot_Oblique(slot);
  }
  if (wantBold && !isControlOrSpace(codepoint)) {
    FT_GlyphSlot_Embolden(slot);
  }

  CachedGlyph cg;
  cg.face = activeFace;
  cg.glyphIndex = glyphIndex;
  cg.advanceX = static_cast<int>(std::lround(static_cast<float>(slot->advance.x) / 64.0f));
  cg.rasterized = needTexture;

  if (needTexture && renderer_ && !isControlOrSpace(codepoint)) {
    if (FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL) == 0 &&
        slot->bitmap.width > 0 && slot->bitmap.rows > 0) {
      int w = static_cast<int>(slot->bitmap.width);
      int h = static_cast<int>(slot->bitmap.rows);
      cg.width = w;
      cg.height = h;
      cg.bearingX = slot->bitmap_left;
      cg.bearingY = slot->bitmap_top;

      std::vector<uint8_t> rgba(static_cast<std::size_t>(w) * h * 4);
      for (int row = 0; row < h; ++row) {
        for (int col = 0; col < w; ++col) {
          uint8_t alpha = slot->bitmap.buffer[row * slot->bitmap.pitch + col];
          std::size_t idx = (static_cast<std::size_t>(row) * w + col) * 4;
          rgba[idx + 0] = 255;
          rgba[idx + 1] = 255;
          rgba[idx + 2] = 255;
          rgba[idx + 3] = alpha;
        }
      }

      SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormatFrom(
          rgba.data(), w, h, 32, w * 4, SDL_PIXELFORMAT_RGBA32);
      if (surf) {
        cg.texture = SDL_CreateTextureFromSurface(renderer_, surf);
        if (cg.texture) {
          SDL_SetTextureBlendMode(cg.texture, SDL_BLENDMODE_BLEND);
        }
        SDL_FreeSurface(surf);
      }
    }
  }

  if (cache_.find(key) == cache_.end() && cache_.size() >= MAX_GLYPH_CACHE) {
    invalidateTextures();
  }
  auto res = cache_.insert_or_assign(key, cg);
  return &res.first->second;
}

int TextRenderer::kerningDelta(FT_Face face, int fontSize, FT_UInt prevGlyph,
                               FT_UInt curGlyph) {
  if (!face || prevGlyph == 0 || curGlyph == 0 || !FT_HAS_KERNING(face)) {
    return 0;
  }
  setSize(face, fontSize);
  FT_Vector delta{};
  if (FT_Get_Kerning(face, prevGlyph, curGlyph, FT_KERNING_DEFAULT, &delta) !=
      0) {
    return 0;
  }
  return static_cast<int>(std::lround(static_cast<float>(delta.x) / 64.0f));
}

int TextRenderer::getKerning(uint32_t leftCp, uint32_t rightCp, int fontSize,
                             uint8_t style) {
  if (!face_) {
    return 0;
  }
  const CachedGlyph *lg = getGlyph(leftCp, fontSize, style, false);
  const CachedGlyph *rg = getGlyph(rightCp, fontSize, style, false);
  if (!lg || !rg || lg->face != rg->face) {
    return 0;
  }
  return kerningDelta(lg->face, fontSize, lg->glyphIndex, rg->glyphIndex);
}

void TextRenderer::drawText(SDL_Renderer *renderer, const std::string &text,
                            int x, int y, int fontSize, SDL_Color color,
                            const std::vector<epub::StyleSpan> &spans) {
  if (!renderer || !face_) {
    return;
  }
  FontMetrics fm = setSize(face_, fontSize);
  int penX = x;
  int baselineY = y + fm.ascent;
  std::size_t spanHint = 0;
  FT_Face prevFace = nullptr;
  FT_UInt prevGlyph = 0;

  for (std::size_t i = 0; i < text.size();) {
    std::size_t offset = i;
    Utf8Char ch = decodeUtf8(text, i);
    i += ch.len;
    if (ch.cp < 0x20u || ch.cp == 0x7Fu) {
      continue;
    }
    uint8_t style = styleAt(spans, offset, spanHint);
    const CachedGlyph *g = getGlyph(ch.cp, fontSize, style, true);
    if (!g) {
      continue;
    }
    if (prevFace == g->face) {
      penX += kerningDelta(g->face, fontSize, prevGlyph, g->glyphIndex);
    }
    if (g->texture) {
      SDL_SetTextureColorMod(g->texture, color.r, color.g, color.b);
      SDL_SetTextureAlphaMod(g->texture, color.a);
      SDL_Rect dst{penX + g->bearingX, baselineY - g->bearingY, g->width,
                   g->height};
      SDL_RenderCopy(renderer, g->texture, nullptr, &dst);
    }
    penX += g->advanceX;
    prevFace = g->face;
    prevGlyph = g->glyphIndex;
  }
}

std::pair<int, int>
TextRenderer::measureText(const std::string &text, int fontSize,
                          const std::vector<epub::StyleSpan> &spans) {
  if (!face_) {
    return {0, 0};
  }
  FontMetrics fm = setSize(face_, fontSize);
  int penX = 0;
  std::size_t spanHint = 0;
  FT_Face prevFace = nullptr;
  FT_UInt prevGlyph = 0;

  for (std::size_t i = 0; i < text.size();) {
    std::size_t offset = i;
    Utf8Char ch = decodeUtf8(text, i);
    i += ch.len;
    if (ch.cp < 0x20u || ch.cp == 0x7Fu) {
      continue;
    }
    uint8_t style = styleAt(spans, offset, spanHint);
    const CachedGlyph *g = getGlyph(ch.cp, fontSize, style, false);
    if (g) {
      if (prevFace == g->face) {
        penX += kerningDelta(g->face, fontSize, prevGlyph, g->glyphIndex);
      }
      penX += g->advanceX;
      prevFace = g->face;
      prevGlyph = g->glyphIndex;
    }
  }
  return {penX, fm.lineHeight};
}

void TextRenderer::drawTextCentered(SDL_Renderer *renderer,
                                    const std::string &text, int centerX,
                                    int centerY, int fontSize,
                                    SDL_Color color) {
  auto [w, h] = measureText(text, fontSize);
  drawText(renderer, text, centerX - w / 2, centerY - h / 2, fontSize, color);
}

std::pair<std::vector<WrappedLine>, int>
TextRenderer::wrapLines(const std::string &text, int fontSize, int maxWidth,
                          const std::vector<epub::StyleSpan> &spans) {
  std::vector<WrappedLine> out;
  if (!face_) {
    return {out, static_cast<int>(std::ceil(fontSize * 1.4f))};
  }
  FontMetrics fm = setSize(face_, fontSize);
  maxWidth = std::max(1, maxWidth);

  std::size_t i = 0;
  while (i < text.size()) {
    while (i < text.size() && text[i] == ' ') {
      ++i;
    }
    if (i >= text.size()) {
      break;
    }
    if (text[i] == '\n') {
      ++i;
      continue;
    }

    std::size_t lineStart = i;
    std::size_t lineEnd = i;
    int lineW = 0;
    std::size_t lastBreakEnd = i;
    std::size_t spanHint = 0;
    FT_Face lineFace = nullptr;
    FT_UInt lineGlyph = 0;

    while (i < text.size()) {
      if (text[i] == '\n') {
        lineEnd = i;
        ++i;
        break;
      }

      std::size_t tokStart = i;
      int tokW = 0;
      int joinKern = 0;
      bool firstInTok = true;
      bool isSpaceTok = (text[i] == ' ');
      FT_Face tokFace = lineFace;
      FT_UInt tokGlyph = lineGlyph;

      while (i < text.size() && text[i] != '\n' &&
             ((text[i] == ' ') == isSpaceTok)) {
        std::size_t offset = i;
        Utf8Char ch = decodeUtf8(text, i);
        i += ch.len;
        if (ch.cp < 0x20u || ch.cp == 0x7Fu) {
          continue;
        }
        uint8_t style = styleAt(spans, offset, spanHint);
        const CachedGlyph *g = getGlyph(ch.cp, fontSize, style, false);
        if (g) {
          if (firstInTok) {
            if (lineW > 0 && lineFace == g->face) {
              joinKern = kerningDelta(g->face, fontSize, lineGlyph, g->glyphIndex);
            }
            firstInTok = false;
          } else if (tokFace == g->face) {
            tokW += kerningDelta(g->face, fontSize, tokGlyph, g->glyphIndex);
          }
          tokW += g->advanceX;
          tokFace = g->face;
          tokGlyph = g->glyphIndex;
        }
      }
      std::size_t tokEnd = i;

      if (lineW + joinKern + tokW <= maxWidth || isSpaceTok) {
        lineW += joinKern + tokW;
        lineFace = tokFace;
        lineGlyph = tokGlyph;
        if (!isSpaceTok) {
          lineEnd = tokEnd;
          lastBreakEnd = tokEnd;
        }
        if (lineW > maxWidth && isSpaceTok) {
          break;
        }
      } else if (lastBreakEnd > lineStart) {
        i = tokStart;
        break;
      } else {
        // Single word wider than maxWidth: break by character
        std::size_t ci = lineStart;
        std::size_t cHint = 0;
        int cw = 0;
        FT_Face cFace = nullptr;
        FT_UInt cGlyph = 0;
        while (ci < tokEnd) {
          Utf8Char ch = decodeUtf8(text, ci);
          uint8_t style = styleAt(spans, ci, cHint);
          const CachedGlyph *g = getGlyph(ch.cp, fontSize, style, false);
          int adv = g ? g->advanceX : 0;
          int kern = (g && cFace == g->face)
                         ? kerningDelta(g->face, fontSize, cGlyph, g->glyphIndex)
                         : 0;
          if (ci > lineStart && cw + kern + adv > maxWidth) {
            break;
          }
          cw += kern + adv;
          if (g) {
            cFace = g->face;
            cGlyph = g->glyphIndex;
          }
          ci += ch.len;
        }
        lineEnd = ci;
        i = ci;
        break;
      }
    }

    while (lineEnd > lineStart && text[lineEnd - 1] == ' ') {
      --lineEnd;
    }
    if (lineEnd > lineStart) {
      out.push_back(WrappedLine{
          text.substr(lineStart, lineEnd - lineStart),
          lineStart,
          sliceSpans(spans, lineStart, lineEnd),
      });
    }
  }

  return {out, fm.lineHeight};
}
