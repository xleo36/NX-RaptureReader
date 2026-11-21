#include "app_controller.hpp"
#include "config.hpp"
#include "progress.hpp"
#include "sdl_context.hpp"
#include "sleep_guard.hpp"
#include "text_renderer.hpp"
#include "theme.hpp"
#include "touch.hpp"
#include "ui.hpp"

#ifdef __SWITCH__
#include <switch.h>
#endif

#include <SDL.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string findFontFile(const char *fileName) {
  std::string candidates[] = {
#ifdef __SWITCH__
      std::string("romfs:/fonts/") + fileName,
#endif
      std::string("assets/fonts/") + fileName,
      std::string("romfs/fonts/") + fileName,
  };
  std::error_code ec;
  for (const std::string &p : candidates) {
    if (fs::is_regular_file(p, ec)) {
      return p;
    }
  }
  return candidates[0];
}

std::string findFontPath() { return findFontFile("Lato-Regular.ttf"); }
std::string findItalicFontPath() {
  return findFontFile("Lato-RegularItalic.ttf");
}

std::optional<std::string> run(IConfig &config,
                               const std::vector<std::string> &cliPaths) {
  AppController app;
  app.init(config, cliPaths);

  // Declare SdlContext before SleepGuard, TextRenderer, PageTextureCache, and
  // CoverCache so wake-lock and cached GPU textures clean up before SDL_Quit.
  SdlContext sdl;
  if (auto err = sdl.init("NX-RaptureReader", config.screen.getW(),
                          config.screen.getH())) {
    return err;
  }

  sleep_guard::SleepGuard sleepGuard;
  sleepGuard.update(app.settings.sleepMinutes);

  TextRenderer textRenderer;
  if (auto err = textRenderer.init(findFontPath(), sdl.renderer(),
                                   findItalicFontPath())) {
    return err;
  }

  ui::PageTextureCache pageTextures;
  ui::CoverCache coverCache;
  touch::TouchTracker touchTracker;

  int lastW = 0;
  int lastH = 0;
  SDL_Event event;

  while (config.running) {
    int winW = 0;
    int winH = 0;
    SDL_GetWindowSize(sdl.window(), &winW, &winH);

    int w = 0;
    int h = 0;
    SDL_GetRendererOutputSize(sdl.renderer(), &w, &h);
    int lw = (app.settings.rotation % 2 == 1) ? h : w;
    int lh = (app.settings.rotation % 2 == 1) ? w : h;

    auto toggleFullscreen = [&]() { sdl.toggleFullscreen(); };

    auto processEvent = [&](const SDL_Event &ev) {
      sdl.handleJoystickEvent(ev);
      if (sleep_guard::isUserActivity(ev)) {
        sleepGuard.noteActivity();
      }
      if (ev.type == SDL_RENDER_TARGETS_RESET ||
          ev.type == SDL_RENDER_DEVICE_RESET) {
        sdl.invalidateTarget();
        textRenderer.invalidateTextures();
        pageTextures.clear();
        coverCache.clear();
        app.needsRedraw = true;
      } else if (ev.type == SDL_WINDOWEVENT) {
        app.needsRedraw = true;
      }

      AppCommand cmd = config.handleEvent(ev);
      if (cmd != AppCommand::None) {
        app.dispatchCommand(cmd, config, sleepGuard, lw, lh, w, h,
                            toggleFullscreen);
        app.needsRedraw = true;
      }

      touch::LibraryLayout libLayout{lh, app.entries.size(), app.selected};
      touch::TocLayout tocLayout{
          app.tocOpen && app.current.has_value(), lh,
          app.current ? app.current->tocCount() : 0, app.tocSelected,
          app.tocTop};
      bool inComic = app.current && app.current->isComic();
      bool inPdf = app.current && app.current->isPdf();
      bool inZoomed = app.isVisualZoomed(lw, lh);
      touch::TouchResult tr = touchTracker.handleEvent(
          ev, app.settings.rotation, winW, winH, app.current.has_value(),
          libLayout, app.settings.showHud, tocLayout, inComic, inPdf,
          inZoomed);
      app.applyTouch(tr, config, sleepGuard, lw, lh, w, h, toggleFullscreen);
    };

    // Sleep cleanly until an event arrives or a toast/sleep timeout needs service.
    if (!app.needsRedraw) {
      int waitMs = 250;
      if (app.toast) {
        auto rem = std::chrono::duration_cast<std::chrono::milliseconds>(
                       app.toast->until - std::chrono::steady_clock::now())
                       .count();
        waitMs = std::clamp(static_cast<int>(rem), 16, waitMs);
      }
      if (SDL_WaitEventTimeout(&event, waitMs) == 1) {
        processEvent(event);
      }
    }
    while (SDL_PollEvent(&event)) {
      processEvent(event);
    }

    app.commitTransitions(config, [&]() {
      pageTextures.clear();
      coverCache.clear();
    });

    sleepGuard.update(app.settings.sleepMinutes);

    if (app.toast && std::chrono::steady_clock::now() >= app.toast->until) {
      app.toast.reset();
      app.needsRedraw = true;
    }

    SDL_GetRendererOutputSize(sdl.renderer(), &w, &h);
    if (w != lastW || h != lastH) {
      lastW = w;
      lastH = h;
      app.needsRedraw = true;
    }

    if (!app.needsRedraw) {
      continue;
    }

    lw = (app.settings.rotation % 2 == 1) ? h : w;
    lh = (app.settings.rotation % 2 == 1) ? w : h;
    if (lw <= 0 || lh <= 0) {
      SDL_Delay(100);
      continue;
    }

    const Theme activeTheme = theme::applyBrightness(
        theme::get(app.settings.theme), app.settings.brightness);

    if (app.current) {
      if (!app.current->isVisual()) {
        SDL_Rect area = ui::readerArea(lw, lh, app.settings.marginPercent);
        app.current->epub().ensureLayout(textRenderer, area.w, area.h,
                                         app.settings.fontSize,
                                         app.settings.lineSpacing);
      }
      if (app.tocOpen) {
        std::size_t count = app.current->tocCount();
        if (count > 0 && app.tocSelected >= count) {
          app.tocSelected = count - 1;
        }
        app.tocTop = ui::clampTocTop(app.tocTop, app.tocSelected, count,
                                     ui::tocVisibleRows(lh));
      }
    }

    SDL_Texture *target = sdl.ensureTarget(lw, lh);
    if (!target) {
      return std::string("Render target error: ") + SDL_GetError();
    }

    const char *toastText = app.toast ? app.toast->text.c_str() : nullptr;
    const std::string sleepLabel = app.settings.sleepLabel();
    const std::string brightnessLabel = app.settings.brightnessLabel();
    const std::string lineSpacingLabel = app.settings.lineSpacingLabel();
    const std::string marginLabel = app.settings.marginLabel();
    const std::string dirLabel = app.settings.readingDirectionLabel();

    std::string decodeErr;
    SDL_Texture *pageTex = nullptr;
    int pageImgW = 0;
    int pageImgH = 0;
    auto [pdfW, pdfH] = app.pdfRenderBounds(lw, lh);
    if (app.current && app.current->isComic() && !app.tocOpen) {
      pageTex = pageTextures.getComic(sdl.renderer(), app.current->path,
                                      app.current->comic(), pageImgW, pageImgH,
                                      decodeErr);
      app.lastVisualImgW = pageImgW;
      app.lastVisualImgH = pageImgH;
    } else if (app.current && app.current->isPdf() && !app.tocOpen) {
      pageTex = pageTextures.getPdf(sdl.renderer(), app.current->path,
                                    app.current->pdfDoc(), pdfW, pdfH, pageImgW,
                                    pageImgH, decodeErr);
      app.lastVisualImgW = pageImgW;
      app.lastVisualImgH = pageImgH;
    }

    app.consumeAlignTop();

    SDL_SetRenderTarget(sdl.renderer(), target);
    if (app.current && app.tocOpen) {
      if (app.current->isComic()) {
        ui::drawComicPages(sdl.renderer(), textRenderer, activeTheme,
                           app.current->comic(), app.tocSelected, app.tocTop,
                           lw, lh, config.tocHint, toastText);
      } else if (app.current->isPdf()) {
        ui::drawPdfPages(sdl.renderer(), textRenderer, activeTheme,
                         app.current->pdfDoc(), app.tocSelected, app.tocTop, lw,
                         lh, config.tocHint, toastText);
      } else {
        ui::drawToc(sdl.renderer(), textRenderer, activeTheme,
                    app.current->epub(), app.tocSelected, app.tocTop, lw, lh,
                    config.tocHint, toastText);
      }
    } else if (app.current && app.current->isVisual()) {
      const std::string &vTitle = app.current->isComic()
                                      ? app.current->comic().title()
                                      : app.current->pdfDoc().title();
      const std::string &vAuthor = app.current->isComic()
                                       ? app.current->comic().author()
                                       : app.current->pdfDoc().author();
      std::size_t vIdx = app.current->isComic()
                             ? app.current->comic().currentPageIndex()
                             : app.current->pdfDoc().currentPageIndex();
      std::size_t vCount = app.current->isComic()
                               ? app.current->comic().pageCount()
                               : app.current->pdfDoc().pageCount();
      ui::drawVisualPage(
          sdl.renderer(), textRenderer, activeTheme, vTitle, vAuthor, vIdx,
          vCount, app.current->permille(), pageTex, pageImgW, pageImgH, lw, lh,
          app.settings.brightness, app.zoomMode, app.panX, app.panY,
          app.current->isComic() ? &dirLabel : nullptr, sleepLabel,
          brightnessLabel, app.settings.showHud, toastText,
          decodeErr.empty() ? nullptr : decodeErr.c_str());
    } else if (app.current) {
      ui::drawReader(sdl.renderer(), textRenderer, activeTheme,
                     app.current->epub(), lw, lh, app.settings.marginPercent,
                     sleepLabel, brightnessLabel, lineSpacingLabel, marginLabel,
                     app.settings.showHud, toastText);
    } else {
      ui::drawLibrary(sdl.renderer(), textRenderer, activeTheme, app.entries,
                      app.selected, lw, lh, config.libraryHint,
                      app.emptyMessage, sleepLabel, brightnessLabel, toastText,
                      &coverCache, app.settings.brightness);
    }
    SDL_SetRenderTarget(sdl.renderer(), nullptr);

    SDL_SetRenderDrawColor(sdl.renderer(), activeTheme.bg.r, activeTheme.bg.g,
                           activeTheme.bg.b, activeTheme.bg.a);
    SDL_RenderClear(sdl.renderer());

    SDL_Rect dest{(w - lw) / 2, (h - lh) / 2, lw, lh};
    double angle = static_cast<double>(app.settings.rotation) * 90.0;
    SDL_RenderCopyEx(sdl.renderer(), target, nullptr, &dest, angle, nullptr,
                     SDL_FLIP_NONE);
    SDL_RenderPresent(sdl.renderer());
    app.needsRedraw = false;

    if (app.current) {
      auto anchor = app.current->anchor();
      if (anchor != app.current->lastAnchor) {
        if (anchor) {
          progress::Progress p{*anchor, app.current->permille()};
          std::string err;
          if (!progress::save(app.progressDir, app.current->path, p, err)) {
            std::cerr << "Could not save progress: " << err << '\n';
          }
        }
        app.current->lastAnchor = anchor;
      }

      if (app.current->isVisual() && !app.tocOpen &&
          !SDL_HasEvents(SDL_FIRSTEVENT, SDL_LASTEVENT)) {
        if (app.current->isComic()) {
          pageTextures.prefetchComic(
              app.current->path, app.current->comic().currentPageIndex() + 1,
              app.current->comic());
        } else {
          pageTextures.prefetchPdf(
              app.current->path, app.current->pdfDoc().currentPageIndex() + 1,
              app.current->pdfDoc(), pdfW, pdfH);
        }
      }
    }
  }

  return std::nullopt;
}

} // namespace

int main(int argc, char *argv[]) {
#ifdef __SWITCH__
  romfsInit();
#endif

  bool forceSwitch = false;
  std::vector<std::string> cliPaths;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--switch") {
      forceSwitch = true;
    } else if (arg.rfind("--", 0) != 0) {
      cliPaths.push_back(std::move(arg));
    }
  }

  std::unique_ptr<IConfig> config;
#ifdef __SWITCH__
  (void)forceSwitch;
  config = std::make_unique<Sconfig>();
#else
  if (forceSwitch) {
    config = std::make_unique<Sconfig>();
  } else {
    config = std::make_unique<Pconfig>();
  }
#endif

  auto err = run(*config, cliPaths);
  if (err) {
    std::cerr << "Error: " << *err << '\n';
  }

#ifdef __SWITCH__
  romfsExit();
#endif
  return err ? 1 : 0;
}
