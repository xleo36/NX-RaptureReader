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
  WindowConfig cfg;

public:
  WindowWrapper() = default;

  ~WindowWrapper() {
    if (window) {
      SDL_DestroyWindow(window);
    }
  }

  WindowWrapper(const WindowWrapper &) = delete;
  WindowWrapper &operator=(const WindowWrapper &) = delete;

  WindowWrapper(WindowWrapper &&other) noexcept
      : window(other.window), cfg(other.cfg) {
    other.window = nullptr;
  }

  WindowWrapper &operator=(WindowWrapper &&other) noexcept {
    if (this != &other) {
      if (window) {
        SDL_DestroyWindow(window);
      }
      window = other.window;
      cfg = other.cfg;
      other.window = nullptr;
    }
    return *this;
  }

  optional<string> create(const WindowConfig &config) {
    cfg = config;
    window = SDL_CreateWindow(cfg.title, cfg.x, cfg.y, cfg.w, cfg.h, cfg.flags);
    if (!window) {
      return string("window wrapper: creating SDL window: ") + SDL_GetError();
    }
    return nullopt;
  }

  SDL_Window *get() const { return window; }
  int getWidth() const { return cfg.w; }
  int getHeight() const { return cfg.h; }
  const WindowConfig &getConfig() const { return cfg; }
};
