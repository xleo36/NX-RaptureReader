#include "app_controller.hpp"
#include "epub.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>

namespace fs = std::filesystem;

namespace {

std::optional<OpenBook> openBookFile(const std::string &path,
                                     const std::string &progressDir,
                                     std::string &err) {
  auto saved = progress::load(progressDir, path);
  if (library::isComicPath(path)) {
    auto comic = cbz::ComicReader::open(path, err);
    if (!comic) {
      return std::nullopt;
    }
    if (saved) {
      comic->restore(saved->anchor);
    }
    auto lastAnchor = comic->anchor();
    return OpenBook{std::move(*comic), path, lastAnchor};
  }
  if (library::isPdfPath(path)) {
    auto doc = pdf::PdfReader::open(path, err);
    if (!doc) {
      return std::nullopt;
    }
    if (saved) {
      doc->restore(saved->anchor);
    }
    auto lastAnchor = doc->anchor();
    return OpenBook{std::move(*doc), path, lastAnchor};
  }

  auto book = epub::load(path, err);
  if (!book) {
    return std::nullopt;
  }
  reader::Reader r(std::move(*book));
  if (saved) {
    r.restore(saved->anchor);
  }
  auto lastAnchor = r.anchor();
  return OpenBook{std::move(r), path, lastAnchor};
}

const char *openingHint(const OpenBook &book, const IConfig &config) {
  if (book.isComic()) {
    return config.comicHint;
  }
  if (book.isPdf()) {
    return config.pdfHint;
  }
  return config.readerHint;
}

void persistSettings(const Settings &settings, const std::string &dir) {
  std::string err;
  if (!settings.save(dir, err)) {
    std::cerr << "Could not save settings: " << err << '\n';
  }
}

} // namespace

std::optional<reader::Anchor> OpenBook::anchor() const {
  if (isComic()) {
    return comic().anchor();
  }
  if (isPdf()) {
    return pdfDoc().anchor();
  }
  return epub().anchor();
}

uint32_t OpenBook::permille() const {
  if (isComic()) {
    return comic().permille();
  }
  if (isPdf()) {
    return pdfDoc().permille();
  }
  return epub().permille();
}

std::size_t OpenBook::tocCount() const {
  if (isComic()) {
    return comic().pageCount();
  }
  if (isPdf()) {
    return pdfDoc().pageCount();
  }
  return epub().chapterCount();
}

std::size_t OpenBook::tocCurrentIndex() const {
  if (isComic()) {
    return comic().currentPageIndex();
  }
  if (isPdf()) {
    return pdfDoc().currentPageIndex();
  }
  return epub().currentChapterIndex();
}

void OpenBook::gotoTocIndex(std::size_t idx) {
  if (isComic()) {
    comic().gotoPage(idx);
  } else if (isPdf()) {
    pdfDoc().gotoPage(idx);
  } else {
    epub().gotoChapter(idx);
  }
}

Toast Toast::make(std::string msg, double secs) {
  auto dur = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
      std::chrono::duration<double>(secs));
  return Toast{std::move(msg), std::chrono::steady_clock::now() + dur};
}

void AppController::init(const IConfig &config,
                         const std::vector<std::string> &cliPaths,
                         const std::string &customProgressDir) {
  progressDir = customProgressDir.empty() ? progress::defaultDir()
                                          : customProgressDir;
  settings = Settings::load(progressDir);

  sources = cliPaths.empty() ? library::defaultSources() : cliPaths;
  entries = library::scan(sources, progressDir);
  selected = 0;
  toast.reset();
  current.reset();
  tocOpen = false;
  tocSelected = 0;
  tocTop = 0;
  zoomMode = cbz::ZoomMode::FitPage;
  panX = 0.0f;
  panY = 0.0f;
  lastVisualImgW = 0;
  lastVisualImgH = 0;
  alignTopPending = false;
  leaveBook = false;
  openSelected = false;
  needsRedraw = true;

  emptyMessage = "No books found. Copy .epub, .cbz, or .pdf files to: ";
  for (std::size_t i = 0; i < sources.size(); ++i) {
    if (i > 0) {
      emptyMessage += "  or  ";
    }
    emptyMessage += sources[i];
  }

  std::error_code ec;
  std::optional<std::string> direct;
  if (cliPaths.size() == 1 && fs::is_regular_file(cliPaths[0], ec)) {
    direct = cliPaths[0];
  } else if (cliPaths.empty() && settings.lastBook &&
             fs::is_regular_file(*settings.lastBook, ec)) {
    direct = *settings.lastBook;
  }

  if (direct) {
    std::string err;
    if (auto book = openBookFile(*direct, progressDir, err)) {
      settings.lastBook = *direct;
      persistSettings(settings, progressDir);
      toast = Toast::make(openingHint(*book, config), 6.0);
      current = std::move(book);
    } else {
      toast = Toast::make("Cannot open book: " + err, 6.0);
    }
  }
}

void AppController::openToc(int lh) {
  if (!current) {
    return;
  }
  std::size_t count = current->tocCount();
  std::size_t visible = ui::tocVisibleRows(lh);
  tocSelected = current->tocCurrentIndex();
  if (count > 0 && tocSelected >= count) {
    tocSelected = count - 1;
  }
  tocTop = (count <= visible)
               ? 0
               : std::min((tocSelected > visible / 2)
                              ? (tocSelected - visible / 2)
                              : 0,
                          count - visible);
  tocOpen = true;
  toast.reset();
}

void AppController::dispatchCommand(
    AppCommand cmd, IConfig &config, sleep_guard::SleepGuard &sleepGuard,
    int &lw, int &lh, int w, int h,
    const std::function<void()> &toggleFullscreen) {
  auto toggleDirection = [&]() {
    settings.toggleReadingDirection();
    persistSettings(settings, progressDir);
    toast = Toast::make(std::string("Reading order: ") +
                            settings.readingDirectionDescription(),
                        1.5);
  };

  auto resetVisualPan = [&]() {
    panX = 0.0f;
    alignTopPending = (zoomMode == cbz::ZoomMode::FitWidth);
    if (!alignTopPending) {
      panY = 0.0f;
    }
  };

  switch (cmd) {
  case AppCommand::None:
  case AppCommand::Quit:
    break;

  case AppCommand::FontIncrease:
  case AppCommand::FontDecrease: {
    int dir = (cmd == AppCommand::FontIncrease) ? 1 : -1;
    if (current && current->isVisual()) {
      cbz::ZoomMode nextZoom = cbz::stepZoom(zoomMode, dir);
      bool changed = (nextZoom != zoomMode);
      zoomMode = nextZoom;
      if (zoomMode == cbz::ZoomMode::FitWidth ||
          zoomMode == cbz::ZoomMode::FitPage) {
        resetVisualPan();
      }
      toast = Toast::make(std::string("Zoom: ") +
                              cbz::zoomDescription(zoomMode) +
                              (changed ? "" : " (limit)"),
                          1.5);
    } else {
      bool changed = settings.changeFont(dir);
      persistSettings(settings, progressDir);
      toast = Toast::make("Font size: " + std::to_string(settings.fontSize) +
                              (changed ? "" : " (limit)"),
                          1.5);
    }
    break;
  }

  case AppCommand::CycleZoom: {
    if (current && current->isVisual()) {
      zoomMode = cbz::cycleZoom(zoomMode);
      resetVisualPan();
      toast = Toast::make(
          std::string("Zoom: ") + cbz::zoomDescription(zoomMode), 1.5);
    }
    break;
  }

  case AppCommand::ToggleReadingDirection: {
    toggleDirection();
    break;
  }

  case AppCommand::CycleTheme: {
    if (config.comicXTogglesDirection && current && current->isComic() &&
        !tocOpen) {
      toggleDirection();
    } else {
      settings.theme = (settings.theme + 1) % theme::THEMES.size();
      persistSettings(settings, progressDir);
      toast = Toast::make(
          std::string("Theme: ") + theme::get(settings.theme).name, 1.5);
    }
    break;
  }

  case AppCommand::Rotate: {
    settings.rotation = static_cast<uint8_t>((settings.rotation + 1) % 4);
    persistSettings(settings, progressDir);
    lw = (settings.rotation % 2 == 1) ? h : w;
    lh = (settings.rotation % 2 == 1) ? w : h;
    toast = Toast::make(
        std::string("Orientation: ") + settings.rotationName(), 1.5);
    break;
  }

  case AppCommand::CycleSleepTimer: {
    settings.cycleSleepTimer();
    persistSettings(settings, progressDir);
    sleepGuard.noteActivity();
    sleepGuard.update(settings.sleepMinutes);
    toast = Toast::make(
        std::string("Screen sleep: ") + settings.sleepDescription(), 1.5);
    break;
  }

  case AppCommand::CycleBrightness: {
    settings.cycleBrightness();
    persistSettings(settings, progressDir);
    toast = Toast::make("Brightness: " + settings.brightnessLabel(), 1.5);
    break;
  }

  case AppCommand::CycleLineSpacing: {
    settings.cycleLineSpacing();
    persistSettings(settings, progressDir);
    toast = Toast::make("Line spacing: " + settings.lineSpacingLabel(), 1.5);
    break;
  }

  case AppCommand::CycleMargins: {
    settings.cycleMargins();
    persistSettings(settings, progressDir);
    toast = Toast::make("Margins: " + settings.marginDescription(), 1.5);
    break;
  }

  case AppCommand::ToggleHud: {
    if (current && !tocOpen) {
      settings.showHud = !settings.showHud;
      persistSettings(settings, progressDir);
      toast = Toast::make(
          settings.showHud ? "HUD shown" : "HUD hidden (tap center to show)",
          1.5);
    }
    break;
  }

  case AppCommand::ToggleToc: {
    if (current) {
      if (tocOpen) {
        tocOpen = false;
      } else {
        openToc(lh);
      }
    }
    break;
  }

  case AppCommand::ToggleFullscreen:
    if (toggleFullscreen) {
      toggleFullscreen();
    }
    break;

  case AppCommand::NextPage:
  case AppCommand::PrevPage:
  case AppCommand::Up:
  case AppCommand::Down:
  case AppCommand::Confirm:
  case AppCommand::Back:
    if (current && tocOpen) {
      std::size_t count = current->tocCount();
      std::size_t visible = ui::tocVisibleRows(lh);
      tocTop = ui::clampTocTop(tocTop, tocSelected, count, visible);
      switch (cmd) {
      case AppCommand::Up:
        if (tocSelected > 0) {
          --tocSelected;
          tocTop = ui::clampTocTop(tocTop, tocSelected, count, visible);
        }
        break;
      case AppCommand::Down:
        if (tocSelected + 1 < count) {
          ++tocSelected;
          tocTop = ui::clampTocTop(tocTop, tocSelected, count, visible);
        }
        break;
      case AppCommand::PrevPage:
        if (count > 0) {
          if (tocTop > 0) {
            tocTop = (tocTop > visible) ? (tocTop - visible) : 0;
            std::size_t pageLast = std::min(count - 1, tocTop + visible - 1);
            tocSelected =
                (tocSelected > visible) ? (tocSelected - visible) : 0;
            tocSelected = std::clamp(tocSelected, tocTop, pageLast);
          } else {
            tocSelected = 0;
          }
        }
        break;
      case AppCommand::NextPage:
        if (count > 0) {
          std::size_t maxTop = (count > visible) ? (count - visible) : 0;
          if (tocTop < maxTop) {
            tocTop = std::min(maxTop, tocTop + visible);
            std::size_t pageLast = std::min(count - 1, tocTop + visible - 1);
            tocSelected = std::clamp(
                std::min(count - 1, tocSelected + visible), tocTop, pageLast);
          } else {
            tocSelected = count - 1;
          }
        }
        break;
      case AppCommand::Confirm:
        if (tocSelected < count) {
          current->gotoTocIndex(tocSelected);
          if (current->isVisual()) {
            resetVisualPan();
          }
        }
        tocOpen = false;
        break;
      case AppCommand::Back:
        tocOpen = false;
        break;
      default:
        break;
      }
    } else if (current && current->isVisual()) {
      auto turnVisual = [&](int dir) {
        if (current->isComic()) {
          if (dir > 0) {
            current->comic().nextPage();
          } else {
            current->comic().prevPage();
          }
        } else {
          if (dir > 0) {
            current->pdfDoc().nextPage();
          } else {
            current->pdfDoc().prevPage();
          }
        }
        resetVisualPan();
      };
      auto jumpVisual = [&](int delta) {
        if (current->isComic()) {
          current->comic().jumpBy(delta);
        } else {
          current->pdfDoc().jumpBy(delta);
        }
        resetVisualPan();
      };
      bool rtl = current->isComic() && settings.mangaRtl;

      switch (cmd) {
      case AppCommand::NextPage:
        turnVisual(rtl ? -1 : 1);
        break;
      case AppCommand::PrevPage:
        turnVisual(rtl ? 1 : -1);
        break;
      case AppCommand::Up:
        if (isVisualZoomed(lw, lh)) {
          panY -= 140.0f;
        } else {
          jumpVisual(-10);
        }
        break;
      case AppCommand::Down:
        if (isVisualZoomed(lw, lh)) {
          panY += 140.0f;
        } else {
          jumpVisual(10);
        }
        break;
      case AppCommand::Confirm:
        openToc(lh);
        break;
      case AppCommand::Back:
        leaveBook = true;
        break;
      default:
        break;
      }
    } else if (current) {
      switch (cmd) {
      case AppCommand::NextPage:
        current->epub().nextPage();
        break;
      case AppCommand::PrevPage:
        current->epub().prevPage();
        break;
      case AppCommand::Up:
        current->epub().prevChapter();
        break;
      case AppCommand::Down:
        current->epub().nextChapter();
        break;
      case AppCommand::Confirm:
        openToc(lh);
        break;
      case AppCommand::Back:
        leaveBook = true;
        break;
      default:
        break;
      }
    } else {
      switch (cmd) {
      case AppCommand::Up:
      case AppCommand::PrevPage:
        if (selected > 0) {
          --selected;
        }
        break;
      case AppCommand::Down:
      case AppCommand::NextPage:
        if (selected + 1 < entries.size()) {
          ++selected;
        }
        break;
      case AppCommand::Confirm:
        openSelected = true;
        break;
      case AppCommand::Back:
        config.quit();
        break;
      default:
        break;
      }
    }
    break;
  }
}

void AppController::applyTouch(const touch::TouchResult &tr, IConfig &config,
                               sleep_guard::SleepGuard &sleepGuard, int &lw,
                               int &lh, int w, int h,
                               const std::function<void()> &toggleFullscreen) {
  if (tr.panDx != 0.0f || tr.panDy != 0.0f) {
    panX += tr.panDx * static_cast<float>(lw);
    panY += tr.panDy * static_cast<float>(lh);
    needsRedraw = true;
  } else if (tr.tocRow && current) {
    current->gotoTocIndex(*tr.tocRow);
    if (current->isVisual()) {
      panX = 0.0f;
      alignTopPending = (zoomMode == cbz::ZoomMode::FitWidth);
      if (!alignTopPending) {
        panY = 0.0f;
      }
    }
    tocOpen = false;
    needsRedraw = true;
  } else if (tr.libraryRow) {
    selected = *tr.libraryRow;
    openSelected = true;
    needsRedraw = true;
  } else if (tr.cmd != AppCommand::None) {
    // Touchscreen Theme tap always cycles theme cleanly even on Switch
    if (tr.cmd == AppCommand::CycleTheme) {
      settings.theme = (settings.theme + 1) % theme::THEMES.size();
      persistSettings(settings, progressDir);
      toast = Toast::make(
          std::string("Theme: ") + theme::get(settings.theme).name, 1.5);
    } else {
      dispatchCommand(tr.cmd, config, sleepGuard, lw, lh, w, h,
                      toggleFullscreen);
    }
    needsRedraw = true;
  }
}

void AppController::commitTransitions(
    const IConfig &config, const std::function<void()> &onBookChanged) {
  if (openSelected && selected < entries.size()) {
    openSelected = false;
    std::string err;
    if (auto book = openBookFile(entries[selected].path, progressDir, err)) {
      if (onBookChanged) {
        onBookChanged();
      }
      settings.lastBook = entries[selected].path;
      persistSettings(settings, progressDir);
      toast = Toast::make(openingHint(*book, config), 6.0);
      current = std::move(book);
      tocOpen = false;
      tocSelected = 0;
      tocTop = 0;
      zoomMode = cbz::ZoomMode::FitPage;
      panX = 0.0f;
      panY = 0.0f;
      lastVisualImgW = 0;
      lastVisualImgH = 0;
      alignTopPending = false;
      needsRedraw = true;
    } else {
      toast = Toast::make("Cannot open book: " + err, 5.0);
      needsRedraw = true;
    }
  }

  if (leaveBook && current) {
    leaveBook = false;
    std::string leftPath = current->path;
    if (onBookChanged) {
      onBookChanged();
    }
    current.reset();
    tocOpen = false;
    tocSelected = 0;
    tocTop = 0;
    zoomMode = cbz::ZoomMode::FitPage;
    panX = 0.0f;
    panY = 0.0f;
    lastVisualImgW = 0;
    lastVisualImgH = 0;
    alignTopPending = false;
    settings.lastBook.reset();
    persistSettings(settings, progressDir);
    library::refreshProgress(entries, progressDir);
    for (std::size_t i = 0; i < entries.size(); ++i) {
      if (entries[i].path == leftPath) {
        selected = i;
        break;
      }
    }
    toast.reset();
    needsRedraw = true;
  }
}

SDL_Rect AppController::visualBounds(int lw, int lh) const {
  if (settings.showHud) {
    return SDL_Rect{0, ui::HEADER_H, lw,
                    std::max(0, lh - (ui::HEADER_H + ui::FOOTER_H))};
  }
  return SDL_Rect{0, 0, lw, lh};
}

bool AppController::isVisualZoomed(int lw, int lh) const {
  if (!current || !current->isVisual() || zoomMode == cbz::ZoomMode::FitPage) {
    return false;
  }
  return cbz::hasOverflow(lastVisualImgW, lastVisualImgH, visualBounds(lw, lh),
                          zoomMode);
}

void AppController::consumeAlignTop() {
  if (alignTopPending) {
    panY = -std::numeric_limits<float>::max();
    alignTopPending = false;
  }
}

std::pair<int, int> AppController::pdfRenderBounds(int lw, int lh) const {
  SDL_Rect b = visualBounds(lw, lh);
  int availW = std::max(1, b.w);
  int availH = std::max(1, b.h);
  double mult = 1.0;
  if (zoomMode == cbz::ZoomMode::FitWidth ||
      zoomMode == cbz::ZoomMode::Zoom150) {
    mult = 1.5;
  } else if (zoomMode == cbz::ZoomMode::Zoom200) {
    mult = 2.0;
  }
  int rw = std::clamp(static_cast<int>(std::lround(availW * mult)), 1, 2560);
  int rh = std::clamp(static_cast<int>(std::lround(availH * mult)), 1, 2560);
  return {rw, rh};
}
