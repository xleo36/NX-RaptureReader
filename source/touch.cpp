#include "touch.hpp"
#include "ui.hpp"

#include <algorithm>
#include <cmath>

namespace touch {
namespace {

float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }

TouchResult cmdResult(AppCommand cmd) {
  return TouchResult{cmd, std::nullopt, std::nullopt, 0.0f, 0.0f};
}

} // namespace

std::pair<float, float> physicalToLogical(float nx, float ny,
                                          uint8_t rotation) {
  switch (rotation % 4) {
  case 0:
    return {nx, ny};
  case 1:
    return {ny, 1.0f - nx};
  case 2:
    return {1.0f - nx, 1.0f - ny};
  default:
    return {1.0f - ny, nx};
  }
}

TouchResult TouchTracker::handleEvent(const SDL_Event &event, uint8_t rotation,
                                      int windowW, int windowH, bool inReader,
                                      const LibraryLayout &lib,
                                      bool hudVisible, const TocLayout &toc,
                                      bool isComic, bool isPdf, bool isZoomed) {
  auto handleMotion = [&](float nx, float ny) -> TouchResult {
    if (!inReader || toc.open || !isZoomed) {
      return {};
    }
    auto [lxStart, lyStart] = physicalToLogical(startNx_, startNy_, rotation);
    auto [lxLast, lyLast] = physicalToLogical(lastNx_, lastNy_, rotation);
    auto [lxNow, lyNow] = physicalToLogical(nx, ny, rotation);
    float totDx = lxNow - lxStart;
    float totDy = lyNow - lyStart;
    if (panned_ || std::hypot(totDx, totDy) > 0.015f) {
      float dLx = lxNow - lxLast;
      float dLy = lyNow - lyLast;
      lastNx_ = nx;
      lastNy_ = ny;
      panned_ = true;
      return TouchResult{AppCommand::None, std::nullopt, std::nullopt, -dLx,
                         -dLy};
    }
    return {};
  };

  if (event.type == SDL_FINGERDOWN) {
    active_ = true;
    isFinger_ = true;
    panned_ = false;
    fingerId_ = event.tfinger.fingerId;
    startNx_ = lastNx_ = clamp01(event.tfinger.x);
    startNy_ = lastNy_ = clamp01(event.tfinger.y);
    return {};
  }

  if (event.type == SDL_FINGERMOTION) {
    if (active_ && isFinger_ && event.tfinger.fingerId == fingerId_) {
      return handleMotion(clamp01(event.tfinger.x), clamp01(event.tfinger.y));
    }
    return {};
  }

  if (event.type == SDL_FINGERUP) {
    if (active_ && isFinger_ && event.tfinger.fingerId == fingerId_) {
      active_ = false;
      if (panned_) {
        panned_ = false;
        return {};
      }
      return finishGesture(clamp01(event.tfinger.x), clamp01(event.tfinger.y),
                           rotation, inReader, lib, hudVisible, toc, isComic,
                           isPdf);
    }
    return {};
  }

  if (event.type == SDL_MOUSEBUTTONDOWN &&
      event.button.button == SDL_BUTTON_LEFT &&
      event.button.which != SDL_TOUCH_MOUSEID && windowW > 0 && windowH > 0) {
    active_ = true;
    isFinger_ = false;
    panned_ = false;
    startNx_ = lastNx_ = clamp01(static_cast<float>(event.button.x) /
                                 static_cast<float>(windowW));
    startNy_ = lastNy_ = clamp01(static_cast<float>(event.button.y) /
                                 static_cast<float>(windowH));
    return {};
  }

  if (event.type == SDL_MOUSEMOTION &&
      event.motion.which != SDL_TOUCH_MOUSEID && windowW > 0 && windowH > 0) {
    if (active_ && !isFinger_ && (event.motion.state & SDL_BUTTON_LMASK)) {
      float nx = clamp01(static_cast<float>(event.motion.x) /
                         static_cast<float>(windowW));
      float ny = clamp01(static_cast<float>(event.motion.y) /
                         static_cast<float>(windowH));
      return handleMotion(nx, ny);
    }
    return {};
  }

  if (event.type == SDL_MOUSEBUTTONUP &&
      event.button.button == SDL_BUTTON_LEFT &&
      event.button.which != SDL_TOUCH_MOUSEID && windowW > 0 && windowH > 0) {
    if (active_ && !isFinger_) {
      active_ = false;
      if (panned_) {
        panned_ = false;
        return {};
      }
      float nx = clamp01(static_cast<float>(event.button.x) /
                         static_cast<float>(windowW));
      float ny = clamp01(static_cast<float>(event.button.y) /
                         static_cast<float>(windowH));
      return finishGesture(nx, ny, rotation, inReader, lib, hudVisible, toc,
                           isComic, isPdf);
    }
    return {};
  }

  return {};
}

TouchResult TouchTracker::finishGesture(float endNx, float endNy,
                                        uint8_t rotation, bool inReader,
                                        const LibraryLayout &lib,
                                        bool hudVisible, const TocLayout &toc,
                                        bool isComic, bool isPdf) {
  auto [lx0, ly0] = physicalToLogical(startNx_, startNy_, rotation);
  auto [lx1, ly1] = physicalToLogical(endNx, endNy, rotation);
  float dx = lx1 - lx0;
  float dy = ly1 - ly0;

  if (inReader) {
    if (toc.open) {
      // Vertical swipe scrolls TOC a page up/down
      if (std::abs(dy) > 0.08f && std::abs(dy) > std::abs(dx) * 1.5f) {
        return cmdResult(dy < 0.0f ? AppCommand::NextPage : AppCommand::PrevPage);
      }
      // Horizontal swipe does not accidentally tap a chapter row or turn pages
      if (std::abs(dx) > 0.08f) {
        return {};
      }
      // Top bar: [< Back] on the left, [Flip] on the right
      if (ly1 < 0.09f) {
        if (lx1 < 0.22f) {
          return cmdResult(AppCommand::Back);
        }
        if (lx1 >= 0.87f) {
          return cmdResult(AppCommand::Rotate);
        }
        return {};
      }
      // Chapter row tap
      if (toc.chapterCount > 0 && toc.logicalHeight > 0) {
        int py = static_cast<int>(ly1 * static_cast<float>(toc.logicalHeight));
        if (py >= ui::TOC_LIST_TOP) {
          std::size_t visible = ui::tocVisibleRows(toc.logicalHeight);
          std::size_t first = ui::clampTocTop(toc.scrollTop, toc.selected,
                                              toc.chapterCount, visible);
          std::size_t rowOffset = static_cast<std::size_t>(
              (py - ui::TOC_LIST_TOP) / ui::TOC_ROW_H);
          if (rowOffset < visible && first + rowOffset < toc.chapterCount) {
            return TouchResult{AppCommand::None, std::nullopt,
                               first + rowOffset, 0.0f, 0.0f};
          }
        }
      }
      return {};
    }

    // Horizontal swipe in logical coordinates turns pages
    if (std::abs(dx) > 0.08f && std::abs(dx) > std::abs(dy) * 1.5f) {
      return cmdResult(dx < 0.0f ? AppCommand::NextPage : AppCommand::PrevPage);
    }

    if (hudVisible) {
      // Top bar controls
      if (ly1 < 0.09f) {
        if (lx1 < 0.16f) {
          return cmdResult(AppCommand::Back);
        }
        if (isComic) {
          if (lx1 >= 0.46f && lx1 < 0.64f) {
            return cmdResult(AppCommand::ToggleReadingDirection);
          }
        } else if (isPdf) {
          if (lx1 >= 0.46f && lx1 < 0.64f) {
            return cmdResult(AppCommand::CycleZoom);
          }
        } else {
          if (lx1 >= 0.46f && lx1 < 0.55f) {
            return cmdResult(AppCommand::FontDecrease);
          }
          if (lx1 >= 0.55f && lx1 < 0.64f) {
            return cmdResult(AppCommand::FontIncrease);
          }
        }
        if (lx1 >= 0.64f && lx1 < 0.74f) {
          return cmdResult(AppCommand::CycleSleepTimer);
        }
        if (lx1 >= 0.74f && lx1 < 0.87f) {
          return cmdResult(AppCommand::CycleTheme);
        }
        if (lx1 >= 0.87f) {
          return cmdResult(AppCommand::Rotate);
        }
        return {};
      }

      // Bottom bar: left controls, centered TOC/page picker, right dimmer
      if (ly1 >= 0.91f) {
        if (isComic) {
          if (lx1 < 0.14f) {
            return cmdResult(AppCommand::ToggleReadingDirection);
          }
          if (lx1 < 0.28f) {
            return cmdResult(AppCommand::CycleZoom);
          }
        } else if (isPdf) {
          if (lx1 < 0.28f) {
            return cmdResult(AppCommand::CycleZoom);
          }
        } else {
          if (lx1 < 0.14f) {
            return cmdResult(AppCommand::CycleLineSpacing);
          }
          if (lx1 < 0.28f) {
            return cmdResult(AppCommand::CycleMargins);
          }
        }
        if (lx1 < 0.78f) {
          return cmdResult(AppCommand::ToggleToc);
        }
        return cmdResult(AppCommand::CycleBrightness);
      }
    }

    // Center zone toggles HUD visibility; left 30% turns back; right turns next
    if (lx1 >= 0.30f && lx1 < 0.70f && ly1 >= 0.25f && ly1 < 0.75f) {
      return cmdResult(AppCommand::ToggleHud);
    }
    return cmdResult(lx1 < 0.30f ? AppCommand::PrevPage : AppCommand::NextPage);
  }

  // Library mode: top bar brightness, sleep timer, theme & flip
  if (ly1 < 0.09f) {
    if (lx1 >= 0.54f && lx1 < 0.64f) {
      return cmdResult(AppCommand::CycleBrightness);
    }
    if (lx1 >= 0.64f && lx1 < 0.74f) {
      return cmdResult(AppCommand::CycleSleepTimer);
    }
    if (lx1 >= 0.74f && lx1 < 0.87f) {
      return cmdResult(AppCommand::CycleTheme);
    }
    if (lx1 >= 0.87f) {
      return cmdResult(AppCommand::Rotate);
    }
    return {};
  }

  // Library mode: tap a visible book row to open it
  if (lib.entryCount > 0 && lib.logicalHeight > 0) {
    int py = static_cast<int>(ly1 * static_cast<float>(lib.logicalHeight));
    if (py >= ui::LIB_LIST_TOP) {
      std::size_t visible = static_cast<std::size_t>(std::max(
          1, (lib.logicalHeight - ui::LIB_LIST_TOP - ui::LIB_BOTTOM_RESERVED) /
                 ui::LIB_ROW_H));
      std::size_t first =
          (lib.selected >= visible) ? (lib.selected + 1 - visible) : 0;
      std::size_t rowOffset =
          static_cast<std::size_t>((py - ui::LIB_LIST_TOP) / ui::LIB_ROW_H);
      if (rowOffset < visible) {
        std::size_t tapped = first + rowOffset;
        if (tapped < lib.entryCount) {
          return TouchResult{AppCommand::None, tapped, std::nullopt, 0.0f,
                             0.0f};
        }
      }
    }
  }

  return {};
}

} // namespace touch
