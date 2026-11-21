#pragma once

#include <SDL.h>
#include <cstdint>
#include <functional>
#include <map>

inline constexpr int DefaultWidth = 1280;
inline constexpr int DefaultHeight = 720;

/// What the user asked for. The app decides what it means on the current screen
/// (e.g. Up/Down move the library selection, or jump chapters while reading).
enum class AppCommand {
  None,
  Quit,
  NextPage,
  PrevPage,
  Up,
  Down,
  Confirm,
  Back,
  FontIncrease,
  FontDecrease,
  CycleTheme,
  Rotate,
  CycleSleepTimer,
  CycleBrightness,
  CycleLineSpacing,
  CycleMargins,
  ToggleHud,
  ToggleToc,
  ToggleReadingDirection,
  CycleZoom,
  ToggleFullscreen,
};

inline bool isRepeatable(AppCommand cmd) {
  switch (cmd) {
  case AppCommand::NextPage:
  case AppCommand::PrevPage:
  case AppCommand::Up:
  case AppCommand::Down:
  case AppCommand::FontIncrease:
  case AppCommand::FontDecrease:
    return true;
  default:
    return false;
  }
}

struct Screen {
  int width = 0;
  int height = 0;

  Screen() = default;
  Screen(int w, int h) : width(w), height(h) {}

  void set(int w, int h) {
    width = w;
    height = h;
  }
  int getW() const { return width; }
  int getH() const { return height; }
};

namespace nx_button {
// Button indices of the Switch SDL2 joystick driver (devkitPro switch-sdl2),
// matching libnx's HidNpadButton bit order.
inline constexpr Uint8 A = 0;
inline constexpr Uint8 B = 1;
inline constexpr Uint8 X = 2;
inline constexpr Uint8 Y = 3;
inline constexpr Uint8 LSTICK = 4;
inline constexpr Uint8 RSTICK = 5;
inline constexpr Uint8 L = 6;
inline constexpr Uint8 R = 7;
inline constexpr Uint8 ZL = 8;
inline constexpr Uint8 ZR = 9;
inline constexpr Uint8 PLUS = 10;
inline constexpr Uint8 MINUS = 11;
inline constexpr Uint8 DPAD_LEFT = 12;
inline constexpr Uint8 DPAD_UP = 13;
inline constexpr Uint8 DPAD_RIGHT = 14;
inline constexpr Uint8 DPAD_DOWN = 15;
} // namespace nx_button

class IConfig {
public:
  virtual ~IConfig() = default;

  Screen screen;
  bool running = true;
  bool comicXTogglesDirection = false;
  const char *libraryHint = "";
  const char *readerHint = "";
  const char *tocHint = "";
  const char *comicHint = "";
  const char *pdfHint = "";

  std::map<SDL_EventType, std::function<AppCommand()>> events;
  std::map<SDL_Keycode, std::function<AppCommand()>> keyBinds;
  std::map<Uint8, std::function<AppCommand()>> joyBinds;

  void quit() { running = false; }

  void onEvent(SDL_EventType type, std::function<AppCommand()> handler) {
    events[type] = std::move(handler);
  }

  void bindKey(SDL_Keycode key, AppCommand cmd) {
    keyBinds[key] = [cmd]() { return cmd; };
  }

  void bindJoy(Uint8 button, AppCommand cmd) {
    joyBinds[button] = [cmd]() { return cmd; };
  }

  /// Dispatches an SDL event through the active bindings and returns the command.
  AppCommand handleEvent(const SDL_Event &event) {
    AppCommand cmd = AppCommand::None;

    if (event.type == SDL_QUIT) {
      auto it = events.find(SDL_QUIT);
      cmd = (it != events.end()) ? it->second() : AppCommand::Quit;
    } else if (event.type == SDL_KEYDOWN) {
      auto it = keyBinds.find(event.key.keysym.sym);
      if (it != keyBinds.end()) {
        AppCommand candidate = it->second();
        // Holding a key repeats navigation and font sizing, but not toggles.
        if (event.key.repeat == 0 || isRepeatable(candidate)) {
          cmd = candidate;
        }
      }
    } else if (event.type == SDL_JOYBUTTONDOWN) {
      auto it = joyBinds.find(event.jbutton.button);
      if (it != joyBinds.end()) {
        cmd = it->second();
      }
    }

    if (cmd == AppCommand::Quit) {
      quit();
    }
    return cmd;
  }
};

// 1. PC / Mac Configuration
class Pconfig : public IConfig {
public:
  Pconfig() {
    screen = Screen(1920, 1080);
    libraryHint =
        "Up/Down select  |  Enter open  |  T theme  |  B dim  |  R rotate  |  S sleep  |  Esc/Q quit";
    readerHint =
        "Left/Right page  |  Up/Down ch  |  C/Enter chapters  |  +/- font  |  L spacing  |  M margins  |  T theme  |  B dim  |  H hud";
    tocHint =
        "Up/Down select  |  PgUp/PgDn scroll  |  Enter jump  |  Esc/C close";
    comicHint =
        "Left/Right page  |  Up/Down pan/±10  |  +/- or Z zoom  |  D RTL/LTR  |  C/Enter pages  |  B dim  |  H hud";
    pdfHint =
        "Left/Right page  |  Up/Down pan/±10  |  +/- or Z zoom  |  C/Enter pages  |  B dim  |  R rotate  |  H hud";

    onEvent(SDL_QUIT, []() { return AppCommand::Quit; });

    bindKey(SDLK_q, AppCommand::Quit);
    bindKey(SDLK_ESCAPE, AppCommand::Back);
    bindKey(SDLK_BACKSPACE, AppCommand::Back);
    bindKey(SDLK_RETURN, AppCommand::Confirm);
    bindKey(SDLK_KP_ENTER, AppCommand::Confirm);

    bindKey(SDLK_RIGHT, AppCommand::NextPage);
    bindKey(SDLK_SPACE, AppCommand::NextPage);
    bindKey(SDLK_PAGEDOWN, AppCommand::NextPage);
    bindKey(SDLK_LEFT, AppCommand::PrevPage);
    bindKey(SDLK_PAGEUP, AppCommand::PrevPage);
    bindKey(SDLK_UP, AppCommand::Up);
    bindKey(SDLK_DOWN, AppCommand::Down);

    bindKey(SDLK_PLUS, AppCommand::FontIncrease);
    bindKey(SDLK_EQUALS, AppCommand::FontIncrease);
    bindKey(SDLK_KP_PLUS, AppCommand::FontIncrease);
    bindKey(SDLK_MINUS, AppCommand::FontDecrease);
    bindKey(SDLK_KP_MINUS, AppCommand::FontDecrease);

    bindKey(SDLK_t, AppCommand::CycleTheme);
    bindKey(SDLK_r, AppCommand::Rotate);
    bindKey(SDLK_s, AppCommand::CycleSleepTimer);
    bindKey(SDLK_b, AppCommand::CycleBrightness);
    bindKey(SDLK_l, AppCommand::CycleLineSpacing);
    bindKey(SDLK_m, AppCommand::CycleMargins);
    bindKey(SDLK_h, AppCommand::ToggleHud);
    bindKey(SDLK_c, AppCommand::ToggleToc);
    bindKey(SDLK_TAB, AppCommand::ToggleToc);
    bindKey(SDLK_d, AppCommand::ToggleReadingDirection);
    bindKey(SDLK_z, AppCommand::CycleZoom);
    bindKey(SDLK_f, AppCommand::ToggleFullscreen);
  }
};

// 2. Nintendo Switch Configuration (Joy-Con / Pro Controller)
class Sconfig : public IConfig {
public:
  Sconfig() {
    screen = Screen(DefaultWidth, DefaultHeight);
    comicXTogglesDirection = true;
    libraryHint =
        "D-Pad select  |  A open  |  X theme  |  L3 dim  |  Y rotate  |  R3 sleep  |  B / + quit";
    readerHint =
        "L/R page  |  Up/Down ch  |  A chapters  |  ZL/ZR font  |  X theme  |  L3 dim  |  - hud  |  Y rotate  |  B lib";
    tocHint = "D-Pad select  |  L/R scroll  |  A jump  |  B close";
    comicHint =
        "L/R page  |  Up/Down pan/±10  |  ZL/ZR zoom  |  X RTL/LTR  |  A pages  |  L3 dim  |  - hud  |  Y rotate  |  B lib";
    pdfHint =
        "L/R page  |  Up/Down pan/±10  |  ZL/ZR zoom  |  A pages  |  L3 dim  |  - hud  |  Y rotate  |  B lib";

    onEvent(SDL_QUIT, []() { return AppCommand::Quit; });

    bindJoy(nx_button::PLUS, AppCommand::Quit);
    bindJoy(nx_button::B, AppCommand::Back);
    bindJoy(nx_button::MINUS, AppCommand::ToggleHud);
    bindJoy(nx_button::A, AppCommand::Confirm);

    bindJoy(nx_button::R, AppCommand::NextPage);
    bindJoy(nx_button::DPAD_RIGHT, AppCommand::NextPage);
    bindJoy(nx_button::L, AppCommand::PrevPage);
    bindJoy(nx_button::DPAD_LEFT, AppCommand::PrevPage);
    bindJoy(nx_button::DPAD_UP, AppCommand::Up);
    bindJoy(nx_button::DPAD_DOWN, AppCommand::Down);

    bindJoy(nx_button::ZR, AppCommand::FontIncrease);
    bindJoy(nx_button::ZL, AppCommand::FontDecrease);
    bindJoy(nx_button::X, AppCommand::CycleTheme);
    bindJoy(nx_button::Y, AppCommand::Rotate);
    bindJoy(nx_button::LSTICK, AppCommand::CycleBrightness);
    bindJoy(nx_button::RSTICK, AppCommand::CycleSleepTimer);
  }
};
