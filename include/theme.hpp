#pragma once

#include <SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

struct Theme {
  const char *name;
  SDL_Color bg;
  SDL_Color text;
  SDL_Color heading;
  /// Dim color for chrome (header, footer, hints, unselected items)
  SDL_Color muted;
};

namespace theme {

// Pure black background for OLED: unlit pixels are fully off, so only the text glows.
// Lowering the Switch brightness leaves only the letters visible in dark rooms.
inline constexpr std::array<Theme, 3> THEMES = {{
    {
        "OLED Black",
        SDL_Color{0, 0, 0, 255},
        SDL_Color{255, 255, 255, 255},
        SDL_Color{255, 255, 255, 255},
        SDL_Color{105, 105, 105, 255},
    },
    {
        "Mocha",
        SDL_Color{30, 30, 46, 255},
        SDL_Color{205, 214, 244, 255},
        SDL_Color{203, 166, 247, 255},
        SDL_Color{127, 132, 156, 255},
    },
    {
        "Paper",
        SDL_Color{244, 236, 216, 255},
        SDL_Color{52, 44, 36, 255},
        SDL_Color{130, 62, 30, 255},
        SDL_Color{150, 138, 118, 255},
    },
}};

inline const Theme &get(std::size_t index) {
  return THEMES[index % THEMES.size()];
}

inline SDL_Color scaleColor(SDL_Color c, float factor) {
  auto scale = [factor](Uint8 channel) -> Uint8 {
    long v = std::lround(static_cast<float>(channel) * factor);
    return static_cast<Uint8>(std::clamp(v, 0L, 255L));
  };
  return SDL_Color{scale(c.r), scale(c.g), scale(c.b), c.a};
}

/// Scales RGB luminance to `percent` (clamped to [30, 100]) for night reading
/// below the Switch's minimum hardware brightness. OLED Black stays pure #000.
inline Theme applyBrightness(const Theme &base, int percent) {
  int clamped = std::clamp(percent, 30, 100);
  if (clamped == 100) {
    return base;
  }
  float factor = static_cast<float>(clamped) / 100.0f;
  return Theme{
      base.name,
      scaleColor(base.bg, factor),
      scaleColor(base.text, factor),
      scaleColor(base.heading, factor),
      scaleColor(base.muted, factor),
  };
}

} // namespace theme
