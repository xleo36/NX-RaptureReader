#include "progress.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace progress {
namespace {

std::string sanitizeBase(const std::string &bookPath) {
  std::string name = fs::path(bookPath).filename().string();
  if (name.empty()) {
    name = "book";
  }
  for (char &c : name) {
    auto uc = static_cast<unsigned char>(c);
    if (!std::isalnum(uc) && c != '.' && c != '-') {
      c = '_';
    }
  }
  return name;
}

uint32_t fnv1aHash32(const std::string &s) {
  uint32_t h = 2166136261u;
  for (unsigned char c : s) {
    h ^= c;
    h *= 16777619u;
  }
  return h;
}

std::optional<Progress> readProgressFile(const std::string &path) {
  std::ifstream in(path);
  if (!in) {
    return std::nullopt;
  }
  std::string data((std::istreambuf_iterator<char>(in)),
                   std::istreambuf_iterator<char>());
  std::istringstream iss(data);
  Progress p;
  if (!(iss >> p.anchor.first >> p.anchor.second)) {
    return std::nullopt;
  }
  if (!(iss >> p.permille)) {
    p.permille = 0;
  }
  return p;
}

} // namespace

std::string defaultDir() {
#ifdef __SWITCH__
  return "sdmc:/switch/NX-RaptureReader";
#else
  const char *home = std::getenv("HOME");
  if (home && home[0] != '\0') {
    fs::path cfg = fs::path(home) / ".config";
    fs::path modern = cfg / "nx-rapturereader";
    fs::path legacy = cfg / "nx-raptureeader";
    std::error_code ec;
    if (!fs::exists(modern, ec) && fs::exists(legacy, ec)) {
      return legacy.string();
    }
    return modern.string();
  }
  return ".";
#endif
}

std::string legacyProgressFile(const std::string &dir,
                               const std::string &bookPath) {
  return (fs::path(dir) / (sanitizeBase(bookPath) + ".pos")).string();
}

std::string progressFile(const std::string &dir, const std::string &bookPath) {
  std::string norm = fs::path(bookPath).lexically_normal().string();
  uint32_t hash = fnv1aHash32(norm);
  char hex[9];
  std::snprintf(hex, sizeof(hex), "%08x", hash);
  return (fs::path(dir) / (sanitizeBase(bookPath) + "_" + hex + ".pos"))
      .string();
}

std::optional<Progress> load(const std::string &dir,
                             const std::string &bookPath) {
  if (auto p = readProgressFile(progressFile(dir, bookPath))) {
    return p;
  }
  return readProgressFile(legacyProgressFile(dir, bookPath));
}

bool save(const std::string &dir, const std::string &bookPath,
          const Progress &p, std::string &err) {
  std::error_code ec;
  fs::create_directories(dir, ec);
  if (ec) {
    err = "Creating " + dir + ": " + ec.message();
    return false;
  }
  std::string path = progressFile(dir, bookPath);
  std::ofstream out(path);
  if (!out) {
    err = "Writing " + path;
    return false;
  }
  out << p.anchor.first << ' ' << p.anchor.second << ' ' << p.permille << '\n';
  if (!out) {
    err = "Writing " + path;
    return false;
  }
  return true;
}

} // namespace progress
