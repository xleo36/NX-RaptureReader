#include <SDL.h>
#include <SDL_error.h>
#include <cstddef>
#include <optional>
#include <window_wrapper.hpp>

using std::nullopt;
using std::optional;
using std::string;

class RendererWrapper {
  WindowWrapper *ww;
  SDL_Renderer *renderer;

public:
  RendererWrapper(WindowWrapper *ww) { ww = ww; };
  optional<string> start() {
    renderer = SDL_CreateRenderer(
        ww->get(), -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
      return string("renderer wrapper: creating SDL renderer: ") +
             SDL_GetError();
    }
    return nullopt;
  }
  ~RendererWrapper() {
    if (renderer) {
      SDL_DestroyRenderer(renderer);
    }
  }
};
