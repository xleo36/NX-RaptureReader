#pragma once

#include "config.hpp"
#include <SDL.h>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

namespace touch {

/// Maps normalized physical screen coordinates `(nx, ny) in [0, 1]^2` into
/// normalized logical coordinates `(lx, ly) in [0, 1]^2` under clockwise
/// quarter-turn rotation `rotation` (0..3).
std::pair<float, float> physicalToLogical(float nx, float ny, uint8_t rotation);

struct TouchResult {
  AppCommand cmd = AppCommand::None;
  /// Set when the user taps a specific book row in the Library screen.
  std::optional<std::size_t> libraryRow;
  /// Set when the user taps a specific chapter/page row in the TOC overlay.
  std::optional<std::size_t> tocRow;
  /// Normalized logical pan delta during an active zoom-drag gesture.
  float panDx = 0.0f;
  float panDy = 0.0f;
};

struct LibraryLayout {
  int logicalHeight = 720;
  std::size_t entryCount = 0;
  std::size_t selected = 0;
};

struct TocLayout {
  bool open = false;
  int logicalHeight = 720;
  std::size_t chapterCount = 0;
  std::size_t selected = 0;
  std::size_t scrollTop = 0;
};

/// Tracks touch (SDL_FINGER*) and mouse (SDL_MOUSEBUTTON*) gestures and maps
/// taps/swipes onto reader & library actions in logical coordinates.
class TouchTracker {
public:
  TouchResult handleEvent(const SDL_Event &event, uint8_t rotation,
                          int windowW, int windowH, bool inReader,
                          const LibraryLayout &lib, bool hudVisible = true,
                          const TocLayout &toc = {}, bool isComic = false,
                          bool isPdf = false, bool isZoomed = false);

private:
  TouchResult finishGesture(float endNx, float endNy, uint8_t rotation,
                            bool inReader, const LibraryLayout &lib,
                            bool hudVisible, const TocLayout &toc,
                            bool isComic, bool isPdf);

  bool active_ = false;
  bool isFinger_ = false;
  bool panned_ = false;
  SDL_FingerID fingerId_ = 0;
  float startNx_ = 0.0f;
  float startNy_ = 0.0f;
  float lastNx_ = 0.0f;
  float lastNy_ = 0.0f;
};

} // namespace touch
