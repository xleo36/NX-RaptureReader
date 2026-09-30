```cpp
#include "config.hpp"
#include <SDL.h>
#include <iostream>
#include <memory>

using std::cout;
using std::endl;

void run(IConfig *config) {
  // 1. Inizializza il sottosistema video di SDL
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) < 0) {
    cout << "Errore inizializzazione SDL: " << SDL_GetError() << endl;
    return;
  }

  int width = config->screen.getW();
  int height = config->screen.getH();

  // 2. Crea finestra e renderer
  SDL_Window *window = SDL_CreateWindow(
      "NX-RaptureReader", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
      width, height, SDL_WINDOW_SHOWN);

  if (!window) {
    cout << "Errore creazione finestra: " << SDL_GetError() << endl;
    SDL_Quit();
    return;
  }

  SDL_Renderer *renderer =
      SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

  if (!renderer) {
    cout << "Errore creazione renderer: " << SDL_GetError() << endl;
    SDL_DestroyWindow(window);
    SDL_Quit();
    return;
  }

  cout << "Finestra creata con successo: " << width << "x" << height << endl;

  // 3. Game Loop / Event Loop
  bool running = true;
  SDL_Event event;

  while (running) {
    // Gestione eventi (Input)
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT) {
        running = false;
      } else if (event.type == SDL_KEYDOWN) {
        // Su PC premiamo ESC per uscire
        if (event.key.keysym.sym == SDLK_ESCAPE) {
          running = false;
        }
      } else if (event.type == SDL_JOYBUTTONDOWN) {
        // Tasto START / '+' sui Joy-Con
        running = false;
      }
    }

    // 4. Rendering: sfondo scuro moderno (#1e1e2e)
    SDL_SetRenderDrawColor(renderer, 30, 30, 46, 255);
    SDL_RenderClear(renderer);

    // Disegniamo una card di prova al centro
    SDL_Rect card = {width / 4, height / 4, width / 2, height / 2};
    SDL_SetRenderDrawColor(renderer, 49, 50, 68, 255);
    SDL_RenderFillRect(renderer, &card);

    // Mostra il frame
    SDL_RenderPresent(renderer);
  }

  // 5. Cleanup
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
}

int main() {
  auto config = std::make_unique<Pconfig>();
  run(config.get());
  return 0;
}
