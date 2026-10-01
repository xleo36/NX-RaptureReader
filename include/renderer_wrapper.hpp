#include <SDL.h>
#include <SDL_error.h>
#include <cstddef>
#include <optional>
#include <window_wrapper.hpp>

using std::nullopt;
using std::optional;
using std::string;

class RendererWrapper {
  WindowWrapper *ww = nullptr;
  SDL_Renderer *renderer = nullptr;

public:
  RendererWrapper(WindowWrapper *ww) : ww(ww), renderer(nullptr) {}

  ~RendererWrapper() {
    if (renderer) {
      SDL_DestroyRenderer(renderer);
    }
  }

  RendererWrapper(const RendererWrapper &) = delete;
  RendererWrapper &operator=(const RendererWrapper &) = delete;

  RendererWrapper(RendererWrapper &&other) noexcept
      : ww(other.ww), renderer(other.renderer) {
    other.renderer = nullptr;
  }

  RendererWrapper &operator=(RendererWrapper &&other) noexcept {
    if (this != &other) {
      if (renderer) {
        SDL_DestroyRenderer(renderer);
      }
      ww = other.ww;
      renderer = other.renderer;
      other.renderer = nullptr;
    }
    return *this;
  }

  optional<string> start() {
    if (!ww || !ww->get()) {
      return string("renderer wrapper: invalid window");
    }
    renderer = SDL_CreateRenderer(
        ww->get(), -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
      return string("renderer wrapper: creating SDL renderer: ") +
             SDL_GetError();
    }
    return nullopt;
  }

  SDL_Renderer *get() const { return renderer; }
  WindowWrapper *getWindowWrapper() const { return ww; }
};
