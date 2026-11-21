#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

inline constexpr int MIN_FONT = 14;
inline constexpr int MAX_FONT = 56;
inline constexpr int FONT_STEP = 2;
inline constexpr int DEFAULT_FONT = 26;

inline constexpr int DEFAULT_SLEEP_MINUTES = 5;
inline constexpr int MAX_SLEEP_MINUTES = 60;
inline constexpr std::array<int, 5> SLEEP_PRESETS = {0, 3, 5, 10, 15};

inline constexpr int DEFAULT_BRIGHTNESS = 100;
inline constexpr int MIN_BRIGHTNESS = 30;
inline constexpr int MAX_BRIGHTNESS = 100;
inline constexpr std::array<int, 4> BRIGHTNESS_PRESETS = {100, 75, 50, 30};

inline constexpr int DEFAULT_LINE_SPACING = 120;
inline constexpr int MIN_LINE_SPACING = 100;
inline constexpr int MAX_LINE_SPACING = 180;
inline constexpr std::array<int, 4> LINE_SPACING_PRESETS = {100, 120, 145, 170};

inline constexpr int DEFAULT_MARGIN_PERCENT = 7;
inline constexpr int MIN_MARGIN_PERCENT = 3;
inline constexpr int MAX_MARGIN_PERCENT = 22;
inline constexpr std::array<int, 4> MARGIN_PRESETS = {4, 7, 12, 18};

struct Settings {
  /// Index into theme::THEMES (0 = OLED Black)
  std::size_t theme = 0;
  /// Body font size in pixels
  int fontSize = DEFAULT_FONT;
  /// Quarter turns clockwise: 0 = landscape, 1 = portrait, 2 = upside-down, 3 = portrait (other way)
  uint8_t rotation = 0;
  /// Reading wake-lock timeout in minutes (0 = Console/OS system default)
  int sleepMinutes = DEFAULT_SLEEP_MINUTES;
  /// Software luminance percentage (30..=100) for dark-room OLED reading
  int brightness = DEFAULT_BRIGHTNESS;
  /// Line height multiplier percentage (100..=180, default 120 = 1.2x)
  int lineSpacing = DEFAULT_LINE_SPACING;
  /// Horizontal margin as percentage of logical screen width (3..=22, default 7%)
  int marginPercent = DEFAULT_MARGIN_PERCENT;
  /// Whether the reader top bar & footer are visible
  bool showHud = true;
  /// Whether `.cbz` comics read Right-to-Left (Manga style)
  bool mangaRtl = true;
  /// Book being read when the app last exited (empty if in the library)
  std::optional<std::string> lastBook;

  static std::string file(const std::string &dir);
  static Settings load(const std::string &dir);
  bool save(const std::string &dir, std::string &err) const;

  /// Grows (+1) or shrinks (-1) the font. Returns false when already at the limit.
  bool changeFont(int direction);
  const char *rotationName() const;

  void cycleSleepTimer();
  std::string sleepLabel() const;
  std::string sleepDescription() const;

  void cycleBrightness();
  std::string brightnessLabel() const;

  void cycleLineSpacing();
  std::string lineSpacingLabel() const;

  void cycleMargins();
  std::string marginLabel() const;
  std::string marginDescription() const;

  void toggleReadingDirection();
  std::string readingDirectionLabel() const;
  const char *readingDirectionDescription() const;

  bool operator==(const Settings &other) const {
    return theme == other.theme && fontSize == other.fontSize &&
           rotation == other.rotation && sleepMinutes == other.sleepMinutes &&
           brightness == other.brightness && lineSpacing == other.lineSpacing &&
           marginPercent == other.marginPercent && showHud == other.showHud &&
           mangaRtl == other.mangaRtl && lastBook == other.lastBook;
  }
  bool operator!=(const Settings &other) const { return !(*this == other); }
};
