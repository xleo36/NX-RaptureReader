#include "config.hpp"
#include <SDL.h>
#include <SDL_error.h>
#include <cstddef>
#include <optional>

using std::nullopt;
using std::optional;
using std::string;

struct WindowConfig {
  const char *title = "";
  int x = SDL_WINDOWPOS_CENTERED;
  int y = SDL_WINDOWPOS_CENTERED;
  int w = DefaultWidth;
  int h = DefaultHeight;
  Uint32 flags = SDL_WINDOW_SHOWN;
};

class WindowWrapper {
  SDL_Window *window = nullptr;

public:
  optional<string> create(const WindowConfig &cfg) {
    window = SDL_CreateWindow(cfg.title, cfg.x, cfg.y, cfg.w, cfg.h, cfg.flags);
    if (!window) {
      return string("window wrapper: creating SDL window: ") + SDL_GetError();
    }
    return nullopt;
  }
  ~WindowWrapper() {
    if (window) {
      SDL_DestroyWindow(window);
    }
  }
  SDL_Window *get() const { return window; }
};
