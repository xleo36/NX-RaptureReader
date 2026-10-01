#include "config.hpp"
#include "renderer_wrapper.hpp"

#include <SDL.h>
#include <iostream>
#include <memory>
#include <optional>

using std::cout;
using std::endl;
using std::nullopt;
using std::optional;
using std::pair;
using std::string;

pair<optional<WindowWrapper>, optional<string>> homeWindow(int width,
                                                           int height) {
  const WindowConfig wconf = WindowConfig{.w = width, .h = height};
  WindowWrapper w;
  optional<string> err = w.create(wconf);
  if (err.has_value()) {
    return pair(nullopt, string("drawing home: ") + err.value());
  }
  return pair(std::move(w), nullopt);
}

optional<string> run(IConfig *config) {
  if (SDL_Init(SDL_INIT_VIDEO) < 0)
    return nullopt;

  auto [home, err] = homeWindow(config->screen.getW(), config->screen.getH());
  if (err) {
    return err.value();
  }

  RendererWrapper rw = RendererWrapper(&home.value());
  err = rw.start();
  if (err) {
    return err.value();
  }
  bool running = true;
  SDL_Event event;

  while (running) {
    // Input
    while (SDL_PollEvent(&event)) {
      // TODO: move these to the global config
      if (event.type == SDL_QUIT) {
        running = false;
      } else if (event.type == SDL_KEYDOWN) {
        // ESC on pc
        if (event.key.keysym.sym == SDLK_ESCAPE) {
          running = false;
        }
      } else if (event.type == SDL_JOYBUTTONDOWN) {
        // START / '+' on Joy-Con
        running = false;
      }
    }

    SDL_SetRenderDrawColor(rw.get(), 30, 30, 46, 255);
    SDL_RenderClear(rw.get());

    int width = rw.getWindowWrapper()->getWidth();
    int height = rw.getWindowWrapper()->getHeight();
    SDL_Rect card = {width / 4, height / 4, width / 2, height / 2};
    SDL_SetRenderDrawColor(rw.get(), 49, 50, 68, 255);
    SDL_RenderFillRect(rw.get(), &card);

    SDL_RenderPresent(rw.get());
  }

  SDL_Quit();
  return nullopt;
}

int main() {
  auto config = std::make_unique<Pconfig>();
  run(config.get());
  return 0;
}
