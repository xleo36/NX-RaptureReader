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

  SDL_Quit();
}

int main() {
  auto config = std::make_unique<Pconfig>();
  run(config.get());
  return 0;
}
