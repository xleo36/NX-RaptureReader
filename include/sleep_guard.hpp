#pragma once

#include <SDL.h>
#include <chrono>
#include <functional>

namespace sleep_guard {

using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;
using BackendFn = std::function<void(bool keepAwake)>;

/// Default OS wake-lock backend: SDL screen saver control on all platforms,
/// plus libnx media playback / auto-sleep / idle extension on Nintendo Switch.
void applySystemWakeLock(bool keepAwake);

/// Returns true for deliberate user input (keys, buttons, D-Pad hat, touch,
/// mouse). Ignores analog stick motion so Joy-Con drift cannot keep the OLED
/// awake indefinitely.
bool isUserActivity(const SDL_Event &event);

/// Keeps the console screen awake like a video player up to `sleepMinutes`
/// after the last deliberate user activity, then releases the wake-lock so
/// Horizon OS / the OS can sleep normally.
class SleepGuard {
public:
  explicit SleepGuard(BackendFn backend = applySystemWakeLock,
                      TimePoint now = Clock::now());
  ~SleepGuard();

  SleepGuard(const SleepGuard &) = delete;
  SleepGuard &operator=(const SleepGuard &) = delete;
  SleepGuard(SleepGuard &&) = delete;
  SleepGuard &operator=(SleepGuard &&) = delete;

  void noteActivity(TimePoint now = Clock::now());
  void update(int sleepMinutes, TimePoint now = Clock::now());
  void release();

  bool isActive() const { return active_; }

private:
  BackendFn backend_;
  TimePoint lastActivity_;
  bool active_ = false;
};

} // namespace sleep_guard
