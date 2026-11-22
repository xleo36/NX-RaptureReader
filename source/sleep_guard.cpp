#include "sleep_guard.hpp"

#ifdef __SWITCH__
#include <switch.h>
#endif

namespace sleep_guard {

void applySystemWakeLock(bool keepAwake) {
  if (keepAwake) {
    SDL_DisableScreenSaver();
  } else {
    SDL_EnableScreenSaver();
  }

#ifdef __SWITCH__
  appletSetMediaPlaybackState(keepAwake);
  appletSetAutoSleepDisabled(keepAwake);
  appletSetIdleTimeDetectionExtension(
      keepAwake ? AppletIdleTimeDetectionExtension_Extended
                : AppletIdleTimeDetectionExtension_None);
#endif
}

bool isUserActivity(const SDL_Event &event) {
  switch (event.type) {
  case SDL_KEYDOWN:
  case SDL_JOYBUTTONDOWN:
  case SDL_JOYHATMOTION:
  case SDL_FINGERDOWN:
  case SDL_FINGERUP:
  case SDL_MOUSEBUTTONDOWN:
  case SDL_MOUSEBUTTONUP:
  case SDL_MOUSEWHEEL:
    return true;
  default:
    return false;
  }
}

SleepGuard::SleepGuard(BackendFn backend, TimePoint now)
    : backend_(backend ? std::move(backend) : applySystemWakeLock),
      lastActivity_(now) {}

SleepGuard::~SleepGuard() { release(); }

void SleepGuard::noteActivity(TimePoint now) { lastActivity_ = now; }

void SleepGuard::update(int sleepMinutes, TimePoint now) {
  bool desired = (sleepMinutes > 0) &&
                 ((now - lastActivity_) < std::chrono::minutes(sleepMinutes));
  if (desired != active_) {
    backend_(desired);
    active_ = desired;
  }
}

void SleepGuard::release() {
  if (active_) {
    backend_(false);
    active_ = false;
  }
}

} // namespace sleep_guard
