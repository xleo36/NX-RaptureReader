#pragma once

#include <SDL.h>
#include <algorithm>
#include <optional>
#include <string>
#include <vector>

/// Owns the entire SDL lifecycle in strict teardown order:
/// offscreen target texture -> renderer -> window -> joysticks -> SDL_Quit.
/// Declare before any SDL texture-owning objects (e.g. TextRenderer) so they
/// destroy their textures before the renderer and SDL shut down.
class SdlContext {
public:
  SdlContext() = default;
  ~SdlContext() { cleanup(); }

  SdlContext(const SdlContext &) = delete;
  SdlContext &operator=(const SdlContext &) = delete;
  SdlContext(SdlContext &&) = delete;
  SdlContext &operator=(SdlContext &&) = delete;

  std::optional<std::string> init(const char *title, int width, int height) {
    cleanup();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) < 0) {
      return std::string("SDL_Init failed: ") + SDL_GetError();
    }
    initialized_ = true;

    // SDL only delivers button events for joysticks that have been opened.
    int numJoy = SDL_NumJoysticks();
    for (int i = 0; i < numJoy; ++i) {
      if (SDL_Joystick *js = SDL_JoystickOpen(i)) {
        joysticks_.push_back(js);
      }
    }

    window_ = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED,
                               SDL_WINDOWPOS_CENTERED, width, height,
                               SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!window_) {
      cleanup();
      return std::string("Creating SDL window: ") + SDL_GetError();
    }

    renderer_ = SDL_CreateRenderer(
        window_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer_) {
      // Fallback for software/dummy video drivers without accelerated vsync
      renderer_ = SDL_CreateRenderer(window_, -1, 0);
    }
    if (!renderer_) {
      cleanup();
      return std::string("Creating SDL renderer: ") + SDL_GetError();
    }

    return std::nullopt;
  }

  void handleJoystickEvent(const SDL_Event &event) {
    if (event.type == SDL_JOYDEVICEADDED) {
      if (SDL_Joystick *js = SDL_JoystickOpen(event.jdevice.which)) {
        joysticks_.push_back(js);
      }
    } else if (event.type == SDL_JOYDEVICEREMOVED) {
      SDL_JoystickID id = event.jdevice.which;
      joysticks_.erase(
          std::remove_if(joysticks_.begin(), joysticks_.end(),
                         [id](SDL_Joystick *js) {
                           if (SDL_JoystickInstanceID(js) == id) {
                             SDL_JoystickClose(js);
                             return true;
                           }
                           return false;
                         }),
          joysticks_.end());
    }
  }

  void toggleFullscreen() {
    if (!window_) {
      return;
    }
    bool full = (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0;
    SDL_SetWindowFullscreen(window_, full ? 0 : SDL_WINDOW_FULLSCREEN);
  }

  SDL_Texture *ensureTarget(int lw, int lh) {
    if (!renderer_ || lw <= 0 || lh <= 0) {
      return nullptr;
    }
    if (!target_ || targetW_ != lw || targetH_ != lh) {
      if (target_) {
        SDL_DestroyTexture(target_);
        target_ = nullptr;
      }
      target_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888,
                                  SDL_TEXTUREACCESS_TARGET, lw, lh);
      if (target_) {
        targetW_ = lw;
        targetH_ = lh;
      } else {
        targetW_ = 0;
        targetH_ = 0;
      }
    }
    return target_;
  }

  void invalidateTarget() {
    if (target_) {
      SDL_DestroyTexture(target_);
      target_ = nullptr;
      targetW_ = 0;
      targetH_ = 0;
    }
  }

  SDL_Window *window() const { return window_; }
  SDL_Renderer *renderer() const { return renderer_; }

private:
  void cleanup() {
    invalidateTarget();
    if (renderer_) {
      SDL_DestroyRenderer(renderer_);
      renderer_ = nullptr;
    }
    if (window_) {
      SDL_DestroyWindow(window_);
      window_ = nullptr;
    }
    for (SDL_Joystick *js : joysticks_) {
      SDL_JoystickClose(js);
    }
    joysticks_.clear();
    if (initialized_) {
      SDL_Quit();
      initialized_ = false;
    }
  }

  bool initialized_ = false;
  SDL_Window *window_ = nullptr;
  SDL_Renderer *renderer_ = nullptr;
  SDL_Texture *target_ = nullptr;
  int targetW_ = 0;
  int targetH_ = 0;
  std::vector<SDL_Joystick *> joysticks_;
};
