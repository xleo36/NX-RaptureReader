#pragma once

#include "epub.hpp"
#include <SDL.h>
#include <cstddef>
#include <cstdint>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_SYNTHESIS_H
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct WrappedLine {
  std::string text;
  std::size_t byteOffset = 0;
  std::vector<epub::StyleSpan> spans;
};

class TextRenderer {
public:
  static constexpr std::size_t MAX_GLYPH_CACHE = 1024;

  TextRenderer() = default;
  ~TextRenderer();

  TextRenderer(const TextRenderer &) = delete;
  TextRenderer &operator=(const TextRenderer &) = delete;

  /// Initializes FreeType from `fontPath` (and optional `italicFontPath`).
  /// Pass `renderer = nullptr` for layout-only/headless tests.
  std::optional<std::string> init(const std::string &fontPath,
                                  SDL_Renderer *renderer,
                                  const std::string &italicFontPath = "");

  /// Destroys all cached GPU glyph textures (e.g. after SDL_RENDER_DEVICE_RESET).
  void invalidateTextures();
  std::size_t cachedGlyphCount() const { return cache_.size(); }

  void drawText(SDL_Renderer *renderer, const std::string &text, int x, int y,
                int fontSize, SDL_Color color,
                const std::vector<epub::StyleSpan> &spans = {});

  void drawTextCentered(SDL_Renderer *renderer, const std::string &text,
                        int centerX, int centerY, int fontSize,
                        SDL_Color color);

  std::pair<int, int>
  measureText(const std::string &text, int fontSize,
              const std::vector<epub::StyleSpan> &spans = {});

  /// Word-wraps `text` to `maxWidth` and returns each visual line with its byte
  /// offset into `text` and clipped style spans, plus the line height in pixels.
  std::pair<std::vector<WrappedLine>, int>
  wrapLines(const std::string &text, int fontSize, int maxWidth,
            const std::vector<epub::StyleSpan> &spans = {});

  /// Exposed for verification: returns pixel kerning adjustment between two codepoints.
  int getKerning(uint32_t leftCp, uint32_t rightCp, int fontSize,
                 uint8_t style = epub::StyleRegular);

private:
  struct CachedGlyph {
    SDL_Texture *texture = nullptr;
    FT_Face face = nullptr;
    FT_UInt glyphIndex = 0;
    int width = 0;
    int height = 0;
    int bearingX = 0;
    int bearingY = 0;
    int advanceX = 0;
    bool rasterized = false;
  };

  struct FontMetrics {
    int ascent = 0;
    int lineHeight = 0;
  };

  void cleanup();
  FontMetrics setSize(FT_Face face, int fontSize);
  const CachedGlyph *getGlyph(uint32_t codepoint, int fontSize, uint8_t style,
                              bool needTexture);
  int kerningDelta(FT_Face face, int fontSize, FT_UInt prevGlyph,
                   FT_UInt curGlyph);

  FT_Library ft_ = nullptr;
  FT_Face face_ = nullptr;
  FT_Face italicFace_ = nullptr;
  SDL_Renderer *renderer_ = nullptr;
  int currentRegularSize_ = 0;
  int currentItalicSize_ = 0;
  std::unordered_map<uint64_t, CachedGlyph> cache_;
};
