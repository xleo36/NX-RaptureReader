#pragma once

#include "cbz.hpp"
#include "config.hpp"
#include "library.hpp"
#include "pdf.hpp"
#include "progress.hpp"
#include "reader.hpp"
#include "settings.hpp"
#include "sleep_guard.hpp"
#include "touch.hpp"
#include "ui.hpp"
#include <SDL.h>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

struct OpenBook {
  std::variant<reader::Reader, cbz::ComicReader, pdf::PdfReader> content;
  std::string path;
  std::optional<reader::Anchor> lastAnchor;

  bool isComic() const {
    return std::holds_alternative<cbz::ComicReader>(content);
  }
  bool isPdf() const {
    return std::holds_alternative<pdf::PdfReader>(content);
  }
  bool isVisual() const { return isComic() || isPdf(); }

  reader::Reader &epub() { return std::get<reader::Reader>(content); }
  const reader::Reader &epub() const {
    return std::get<reader::Reader>(content);
  }
  cbz::ComicReader &comic() { return std::get<cbz::ComicReader>(content); }
  const cbz::ComicReader &comic() const {
    return std::get<cbz::ComicReader>(content);
  }
  pdf::PdfReader &pdfDoc() { return std::get<pdf::PdfReader>(content); }
  const pdf::PdfReader &pdfDoc() const {
    return std::get<pdf::PdfReader>(content);
  }

  std::optional<reader::Anchor> anchor() const;
  uint32_t permille() const;
  std::size_t tocCount() const;
  std::size_t tocCurrentIndex() const;
  void gotoTocIndex(std::size_t idx);
};

struct Toast {
  std::string text;
  std::chrono::steady_clock::time_point until;

  static Toast make(std::string msg, double secs);
};

/// Owns library/reader/TOC state transitions independent of SDL window/GPU objects.
class AppController {
public:
  Settings settings;
  std::string progressDir;
  std::vector<std::string> sources;
  std::vector<library::Entry> entries;
  std::size_t selected = 0;
  std::optional<Toast> toast;
  std::optional<OpenBook> current;
  std::string emptyMessage;

  bool tocOpen = false;
  std::size_t tocSelected = 0;
  std::size_t tocTop = 0;
  cbz::ZoomMode zoomMode = cbz::ZoomMode::FitPage;
  float panX = 0.0f;
  float panY = 0.0f;
  int lastVisualImgW = 0;
  int lastVisualImgH = 0;
  bool alignTopPending = false;
  bool leaveBook = false;
  bool openSelected = false;
  bool needsRedraw = true;

  void init(const IConfig &config, const std::vector<std::string> &cliPaths,
            const std::string &customProgressDir = "");

  void openToc(int lh);
  void dispatchCommand(AppCommand cmd, IConfig &config,
                       sleep_guard::SleepGuard &sleepGuard, int &lw, int &lh,
                       int w, int h,
                       const std::function<void()> &toggleFullscreen = {});
  void applyTouch(const touch::TouchResult &tr, IConfig &config,
                  sleep_guard::SleepGuard &sleepGuard, int &lw, int &lh, int w,
                  int h,
                  const std::function<void()> &toggleFullscreen = {});
  void commitTransitions(const IConfig &config,
                         const std::function<void()> &onBookChanged = {});

  SDL_Rect visualBounds(int lw, int lh) const;
  bool isVisualZoomed(int lw, int lh) const;
  void consumeAlignTop();
  std::pair<int, int> pdfRenderBounds(int lw, int lh) const;
};
