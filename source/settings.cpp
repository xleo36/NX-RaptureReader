#include "settings.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace {

std::string trim(const std::string &s) {
  std::size_t start = 0;
  while (start < s.size() &&
         std::isspace(static_cast<unsigned char>(s[start]))) {
    ++start;
  }
  std::size_t end = s.size();
  while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
    --end;
  }
  return s.substr(start, end - start);
}

template <typename T> bool parseUnsigned(const std::string &s, T &out) {
  if (s.empty()) {
    return false;
  }
  for (unsigned char c : s) {
    if (!std::isdigit(c)) {
      return false;
    }
  }
  std::istringstream iss(s);
  uint64_t v = 0;
  if (!(iss >> v)) {
    return false;
  }
  out = static_cast<T>(v);
  return true;
}

template <std::size_t N>
int cyclePreset(int current, const std::array<int, N> &presets, bool ascending) {
  for (std::size_t i = 0; i < presets.size(); ++i) {
    if (presets[i] == current) {
      return presets[(i + 1) % presets.size()];
    }
  }
  for (int preset : presets) {
    if (ascending ? (preset > current) : (preset < current)) {
      return preset;
    }
  }
  return presets.front();
}

} // namespace

std::string Settings::file(const std::string &dir) {
  return (fs::path(dir) / "settings.cfg").string();
}

Settings Settings::load(const std::string &dir) {
  Settings s;
  std::ifstream in(file(dir));
  if (!in) {
    return s;
  }
  std::string line;
  while (std::getline(in, line)) {
    auto eq = line.find('=');
    if (eq == std::string::npos) {
      continue;
    }
    std::string key = trim(line.substr(0, eq));
    std::string val = trim(line.substr(eq + 1));

    if (key == "theme") {
      std::size_t v = 0;
      if (parseUnsigned(val, v)) {
        s.theme = v;
      }
    } else if (key == "font_size") {
      int v = 0;
      if (parseUnsigned(val, v)) {
        s.fontSize = std::clamp(v, MIN_FONT, MAX_FONT);
      }
    } else if (key == "rotation") {
      unsigned int v = 0;
      if (parseUnsigned(val, v)) {
        s.rotation = static_cast<uint8_t>(v % 4);
      }
    } else if (key == "sleep_minutes") {
      int v = 0;
      if (parseUnsigned(val, v)) {
        s.sleepMinutes = std::clamp(v, 0, MAX_SLEEP_MINUTES);
      }
    } else if (key == "brightness") {
      int v = 0;
      if (parseUnsigned(val, v)) {
        s.brightness = std::clamp(v, MIN_BRIGHTNESS, MAX_BRIGHTNESS);
      }
    } else if (key == "line_spacing") {
      int v = 0;
      if (parseUnsigned(val, v)) {
        s.lineSpacing = std::clamp(v, MIN_LINE_SPACING, MAX_LINE_SPACING);
      }
    } else if (key == "margin_percent") {
      int v = 0;
      if (parseUnsigned(val, v)) {
        s.marginPercent =
            std::clamp(v, MIN_MARGIN_PERCENT, MAX_MARGIN_PERCENT);
      }
    } else if (key == "show_hud") {
      unsigned int v = 1;
      if (parseUnsigned(val, v)) {
        s.showHud = (v != 0);
      }
    } else if (key == "manga_rtl") {
      unsigned int v = 1;
      if (parseUnsigned(val, v)) {
        s.mangaRtl = (v != 0);
      }
    } else if (key == "last_book" && !val.empty()) {
      s.lastBook = val;
    }
  }
  return s;
}

bool Settings::save(const std::string &dir, std::string &err) const {
  std::error_code ec;
  fs::create_directories(dir, ec);
  if (ec) {
    err = "Creating " + dir + ": " + ec.message();
    return false;
  }
  std::ofstream out(file(dir));
  if (!out) {
    err = "Writing settings";
    return false;
  }
  out << "theme=" << theme << '\n'
      << "font_size=" << fontSize << '\n'
      << "rotation=" << static_cast<unsigned int>(rotation) << '\n'
      << "sleep_minutes=" << sleepMinutes << '\n'
      << "brightness=" << brightness << '\n'
      << "line_spacing=" << lineSpacing << '\n'
      << "margin_percent=" << marginPercent << '\n'
      << "show_hud=" << (showHud ? 1 : 0) << '\n'
      << "manga_rtl=" << (mangaRtl ? 1 : 0) << '\n'
      << "last_book=" << (lastBook ? *lastBook : "") << '\n';
  if (!out) {
    err = "Writing settings";
    return false;
  }
  return true;
}

bool Settings::changeFont(int direction) {
  int next = std::clamp(fontSize + direction * FONT_STEP, MIN_FONT, MAX_FONT);
  bool changed = (next != fontSize);
  fontSize = next;
  return changed;
}

const char *Settings::rotationName() const {
  switch (rotation % 4) {
  case 0:
    return "Landscape";
  case 1:
    return "Portrait";
  case 2:
    return "Landscape (flipped)";
  default:
    return "Portrait (flipped)";
  }
}

void Settings::cycleSleepTimer() {
  sleepMinutes = cyclePreset(sleepMinutes, SLEEP_PRESETS, true);
}

std::string Settings::sleepLabel() const {
  if (sleepMinutes <= 0) {
    return "Sys";
  }
  return std::to_string(sleepMinutes) + "m";
}

std::string Settings::sleepDescription() const {
  if (sleepMinutes <= 0) {
    return "System default";
  }
  return std::to_string(sleepMinutes) + " min";
}

void Settings::cycleBrightness() {
  brightness = cyclePreset(brightness, BRIGHTNESS_PRESETS, false);
}

std::string Settings::brightnessLabel() const {
  return std::to_string(brightness) + "%";
}

void Settings::cycleLineSpacing() {
  lineSpacing = cyclePreset(lineSpacing, LINE_SPACING_PRESETS, true);
}

std::string Settings::lineSpacingLabel() const {
  int whole = lineSpacing / 100;
  int frac = lineSpacing % 100;
  if (frac == 0) {
    return std::to_string(whole) + ".0x";
  }
  if (frac % 10 == 0) {
    return std::to_string(whole) + "." + std::to_string(frac / 10) + "x";
  }
  if (frac < 10) {
    return std::to_string(whole) + ".0" + std::to_string(frac) + "x";
  }
  return std::to_string(whole) + "." + std::to_string(frac) + "x";
}

void Settings::cycleMargins() {
  marginPercent = cyclePreset(marginPercent, MARGIN_PRESETS, true);
}

std::string Settings::marginLabel() const {
  return "M:" + std::to_string(marginPercent) + "%";
}

std::string Settings::marginDescription() const {
  std::string pct = std::to_string(marginPercent) + "%";
  if (marginPercent <= 5) {
    return pct + " (Narrow)";
  }
  if (marginPercent <= 9) {
    return pct + " (Normal)";
  }
  if (marginPercent <= 14) {
    return pct + " (Wide)";
  }
  return pct + " (Extra Wide)";
}

void Settings::toggleReadingDirection() { mangaRtl = !mangaRtl; }

std::string Settings::readingDirectionLabel() const {
  return mangaRtl ? "RTL" : "LTR";
}

const char *Settings::readingDirectionDescription() const {
  return mangaRtl ? "Right-to-Left (Manga)" : "Left-to-Right (Comic)";
}
