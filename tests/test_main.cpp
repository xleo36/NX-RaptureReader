#include "app_controller.hpp"
#include "cbz.hpp"
#include "epub.hpp"
#include "library.hpp"
#include "pdf.hpp"
#include "progress.hpp"
#include "reader.hpp"
#include "settings.hpp"
#include "sleep_guard.hpp"
#include "text_renderer.hpp"
#include "theme.hpp"
#include "touch.hpp"
#include "ui.hpp"
#include "zip_reader.hpp"

#include <SDL.h>
#include <webp/encode.h>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

int gPassed = 0;

void runTest(const char *name, void (*fn)()) {
  fn();
  ++gPassed;
  std::cout << "  [OK] " << name << '\n';
}

fs::path tempDir(const std::string &name) {
  fs::path dir = fs::temp_directory_path() / ("nx-raptureeader-cpp-" + name);
  std::error_code ec;
  fs::remove_all(dir, ec);
  return dir;
}

epub::Book longBook() {
  epub::Book book;
  book.title = "Test";
  book.author = "Nobody";
  for (int ch = 0; ch < 3; ++ch) {
    book.chapterStarts.push_back(book.blocks.size());
    book.blocks.push_back(
        epub::Block{epub::BlockKind::Heading,
                    "Chapter " + std::to_string(ch + 1), {}});
    for (int p = 0; p < 30; ++p) {
      book.blocks.push_back(epub::Block{
          epub::BlockKind::Paragraph,
          "Paragraph " + std::to_string(p) + " of chapter " +
              std::to_string(ch) +
              ". The quick brown fox jumps over the lazy dog while the "
              "rabbit hurries past, late for a very important date, and Alice "
              "follows it down the hole without a single thought about getting "
              "out again.",
          {}});
    }
  }
  return book;
}

bool pageContains(const reader::Reader &r, reader::Anchor spot) {
  const auto &pages = r.activeChapterPages();
  std::size_t idx = r.currentPageInChapter();
  reader::Anchor cur = pages[idx].anchor;
  if (spot < cur) {
    return false;
  }
  if (idx + 1 < pages.size()) {
    return spot < pages[idx + 1].anchor;
  }
  return true;
}

void testEpubLoadsSample() {
  std::string err;
  auto book = epub::load("books/Alice_in_Wonderland.epub", err);
  assert(book.has_value());
  assert(book->title == "Alice's Adventures in Wonderland");
  assert(book->author == "Lewis Carroll");
  assert(book->chapterStarts.size() == 3);
  assert(book->blocks[0].kind == epub::BlockKind::Heading);
  assert(book->blocks[0].text == "Chapter I: Down the Rabbit-Hole");
  assert(book->blocks[1].kind == epub::BlockKind::Paragraph);
  assert(book->blocks[1].text.rfind("Alice was beginning", 0) == 0);
}

void testEpubMetadataOnly() {
  std::string err;
  auto meta = epub::metadata("books/Alice_in_Wonderland.epub", err);
  assert(meta.has_value());
  assert(meta->title == "Alice's Adventures in Wonderland");
  assert(meta->author == "Lewis Carroll");
}

void testEpubDecodesEntities() {
  std::string decoded = epub::decodeEntities("a &amp; b &#8217;s &#x41; &bogus;");
  assert(decoded == "a & b \xE2\x80\x99s A &bogus;");
}

void testEpubSplitsBlocks() {
  auto blocks = epub::htmlToBlocks(
      "<html><head><title>x</title></head><body><h1>T</h1><p>a  b\n c</p><p>d</p></body></html>");
  assert(blocks.size() == 3);
  assert(blocks[0].kind == epub::BlockKind::Heading);
  assert(blocks[0].text == "T");
  assert(blocks[1].text == "a b c");
  assert(blocks[2].text == "d");
}

void testProgressRoundtrip() {
  fs::path dir = tempDir("progress-roundtrip");
  std::string book = "books/My Book (1).epub";
  progress::Progress p{{12, 345}, 420};

  assert(!progress::load(dir.string(), book).has_value());
  std::string err;
  assert(progress::save(dir.string(), book, p, err));
  auto loaded = progress::load(dir.string(), book);
  assert(loaded.has_value());
  assert(*loaded == p);
  assert(!progress::load(dir.string(), "other.epub").has_value());

  std::error_code ec;
  fs::remove_all(dir, ec);
}

void testProgressLegacy() {
  fs::path dir = tempDir("progress-legacy");
  fs::create_directories(dir);
  std::string book = "a.epub";
  {
    std::ofstream out(progress::progressFile(dir.string(), book));
    out << "7 99\n";
  }
  auto loaded = progress::load(dir.string(), book);
  assert(loaded.has_value());
  assert((loaded->anchor == std::pair<std::size_t, std::size_t>{7, 99}));
  assert(loaded->permille == 0);

  std::error_code ec;
  fs::remove_all(dir, ec);
}

void testProgressCorrupt() {
  fs::path dir = tempDir("progress-corrupt");
  fs::create_directories(dir);
  std::string book = "a.epub";
  {
    std::ofstream out(progress::progressFile(dir.string(), book));
    out << "garbage";
  }
  assert(!progress::load(dir.string(), book).has_value());

  std::error_code ec;
  fs::remove_all(dir, ec);
}

void testSettingsRoundtrip() {
  fs::path dir = tempDir("settings-roundtrip");
  Settings s;
  s.theme = 2;
  s.fontSize = 34;
  s.rotation = 3;
  s.sleepMinutes = 10;
  s.brightness = 50;
  s.lineSpacing = 145;
  s.marginPercent = 12;
  s.showHud = false;
  s.mangaRtl = false;
  s.lastBook = "books/a=b.epub";

  std::string err;
  assert(s.save(dir.string(), err));
  assert(Settings::load(dir.string()) == s);

  std::error_code ec;
  fs::remove_all(dir, ec);
}

void testSettingsClamping() {
  fs::path dir = tempDir("settings-clamp");
  Settings def = Settings::load(dir.string());
  assert(def == Settings{});
  assert(def.sleepMinutes == DEFAULT_SLEEP_MINUTES);
  assert(def.sleepLabel() == "5m");
  assert(def.brightness == DEFAULT_BRIGHTNESS);
  assert(def.brightnessLabel() == "100%");
  assert(def.lineSpacing == DEFAULT_LINE_SPACING);
  assert(def.lineSpacingLabel() == "1.2x");
  assert(def.marginPercent == DEFAULT_MARGIN_PERCENT);
  assert(def.marginLabel() == "M:7%");
  assert(def.showHud);
  assert(def.mangaRtl);
  assert(def.readingDirectionLabel() == "RTL");

  fs::create_directories(dir);
  {
    std::ofstream out(Settings::file(dir.string()));
    out << "font_size=999\nrotation=9\nsleep_minutes=999\nbrightness=5\n"
           "line_spacing=999\nmargin_percent=1\nshow_hud=0\nmanga_rtl=0\nnonsense\n";
  }
  Settings s = Settings::load(dir.string());
  assert(s.fontSize == MAX_FONT);
  assert(s.rotation == 1);
  assert(s.sleepMinutes == MAX_SLEEP_MINUTES);
  assert(s.brightness == MIN_BRIGHTNESS);
  assert(s.lineSpacing == MAX_LINE_SPACING);
  assert(s.marginPercent == MIN_MARGIN_PERCENT);
  assert(!s.showHud);
  assert(!s.mangaRtl);
  assert(s.readingDirectionLabel() == "LTR");
  s.toggleReadingDirection();
  assert(s.mangaRtl && s.readingDirectionLabel() == "RTL");

  // Cycle sleep presets: 5m -> 10m -> 15m -> Sys (0) -> 3m -> 5m
  s.sleepMinutes = 5;
  s.cycleSleepTimer();
  assert(s.sleepMinutes == 10 && s.sleepLabel() == "10m");
  s.cycleSleepTimer();
  assert(s.sleepMinutes == 15);
  s.cycleSleepTimer();
  assert(s.sleepMinutes == 0 && s.sleepLabel() == "Sys" &&
         s.sleepDescription() == "System default");
  s.cycleSleepTimer();
  assert(s.sleepMinutes == 3);
  s.cycleSleepTimer();
  assert(s.sleepMinutes == 5);

  // Cycle brightness presets: 100% -> 75% -> 50% -> 30% -> 100%
  s.brightness = 100;
  s.cycleBrightness();
  assert(s.brightness == 75 && s.brightnessLabel() == "75%");
  s.cycleBrightness();
  assert(s.brightness == 50);
  s.cycleBrightness();
  assert(s.brightness == 30);
  s.cycleBrightness();
  assert(s.brightness == 100);

  // Cycle line spacing presets: 1.2x -> 1.45x -> 1.7x -> 1.0x -> 1.2x
  s.lineSpacing = 120;
  s.cycleLineSpacing();
  assert(s.lineSpacing == 145 && s.lineSpacingLabel() == "1.45x");
  s.cycleLineSpacing();
  assert(s.lineSpacing == 170 && s.lineSpacingLabel() == "1.7x");
  s.cycleLineSpacing();
  assert(s.lineSpacing == 100 && s.lineSpacingLabel() == "1.0x");
  s.cycleLineSpacing();
  assert(s.lineSpacing == 120 && s.lineSpacingLabel() == "1.2x");

  // Cycle margin presets: 7% -> 12% -> 18% -> 4% -> 7%
  s.marginPercent = 7;
  s.cycleMargins();
  assert(s.marginPercent == 12 && s.marginLabel() == "M:12%");
  s.cycleMargins();
  assert(s.marginPercent == 18);
  s.cycleMargins();
  assert(s.marginPercent == 4 && s.marginLabel() == "M:4%");
  s.cycleMargins();
  assert(s.marginPercent == 7);

  std::error_code ec;
  fs::remove_all(dir, ec);
}

void testSettingsFontLimits() {
  Settings s;
  s.fontSize = MAX_FONT;
  assert(!s.changeFont(1));
  s.fontSize = MIN_FONT;
  assert(!s.changeFont(-1));
  assert(s.changeFont(1));
  assert(s.fontSize == MIN_FONT + FONT_STEP);
}

void testLibraryScan() {
  fs::path dir = tempDir("library");
  auto entries = library::scan({"books"}, dir.string());
  bool foundAlice = false;
  for (const auto &e : entries) {
    if (e.title == "Alice's Adventures in Wonderland") {
      foundAlice = true;
      assert(e.author == "Lewis Carroll");
      assert(!e.permille.has_value());
    }
  }
  assert(foundAlice);

  auto empty = library::scan({"does-not-exist", "CMakeLists.txt"}, dir.string());
  assert(empty.empty());
}

void testReaderBookmarkAcrossFontSizes() {
  TextRenderer text;
  auto err = text.init("assets/fonts/Lato-Regular.ttf", nullptr);
  assert(!err.has_value());

  reader::Reader r(longBook());
  r.ensureLayout(text, 700, 420, 20);
  std::size_t smallPages = r.activeChapterPageCount();
  for (int i = 0; i < 4; ++i) {
    r.nextPage();
  }
  auto spot = r.anchor();
  assert(spot.has_value());
  assert(pageContains(r, *spot));

  r.ensureLayout(text, 700, 420, 40);
  assert(r.activeChapterPageCount() > smallPages);
  assert(pageContains(r, *spot));

  r.ensureLayout(text, 700, 420, 14);
  assert(r.activeChapterPageCount() < smallPages);
  assert(pageContains(r, *spot));

  for (int sz : {30, 18, 44, 22, 20}) {
    r.ensureLayout(text, 700, 420, sz);
    assert(r.anchor() == spot);
    assert(pageContains(r, *spot));
  }
}

void testReaderBookmarkAcrossRotation() {
  TextRenderer text;
  auto err = text.init("assets/fonts/Lato-Regular.ttf", nullptr);
  assert(!err.has_value());

  reader::Reader r(longBook());
  r.ensureLayout(text, 1100, 600, 26);
  for (int i = 0; i < 3; ++i) {
    r.nextPage();
  }
  auto spot = r.anchor();
  assert(spot.has_value());

  r.ensureLayout(text, 600, 1100, 26);
  assert(pageContains(r, *spot));

  r.ensureLayout(text, 1100, 600, 26);
  assert(pageContains(r, *spot));
  assert(r.anchor() == spot);
}

void testReaderRestoreAcrossLayouts() {
  TextRenderer text;
  auto err = text.init("assets/fonts/Lato-Regular.ttf", nullptr);
  assert(!err.has_value());

  reader::Reader first(longBook());
  first.ensureLayout(text, 700, 420, 20);
  for (int i = 0; i < 15; ++i) {
    first.nextPage();
  }
  auto saved = first.anchor();
  assert(saved.has_value());

  reader::Reader second(longBook());
  second.restore(*saved);
  second.ensureLayout(text, 500, 800, 32);
  assert(pageContains(second, *saved));
  assert(second.anchor() == saved);
}

void testReaderChapterNavigationAndProgress() {
  TextRenderer text;
  auto err = text.init("assets/fonts/Lato-Regular.ttf", nullptr);
  assert(!err.has_value());

  reader::Reader r(longBook());
  r.ensureLayout(text, 700, 420, 22);
  assert((r.chapterNumber() == std::pair<std::size_t, std::size_t>{1, 3}));
  assert(r.permille() == 0);

  r.nextChapter();
  assert((r.chapterNumber() == std::pair<std::size_t, std::size_t>{2, 3}));
  assert(r.anchor()->second == 0);
  assert(r.page()->lines[0].heading);

  r.prevChapter();
  assert((r.chapterNumber() == std::pair<std::size_t, std::size_t>{1, 3}));

  uint32_t last = 0;
  for (int guard = 0; guard < 500 && r.permille() < 1000; ++guard) {
    r.nextPage();
    assert(r.permille() >= last);
    last = r.permille();
  }
  assert(r.permille() == 1000);
}

void testReaderMultiPageChapterHeadingRule() {
  TextRenderer text;
  auto err = text.init("assets/fonts/Lato-Regular.ttf", nullptr);
  assert(!err.has_value());

  std::string loadErr;
  auto book = epub::load("books/Alice_in_Wonderland.epub", loadErr);
  assert(book.has_value());

  reader::Reader r(std::move(*book));
  r.ensureLayout(text, 620, 560, 24);

  for (std::size_t ch = 0; ch < 3; ++ch) {
    const auto &pages = r.activeChapterPages();
    assert(pages.size() >= 3); // Real multi-page chapter

    // Page 0 has the chapter title and heading line(s)
    assert(pages[0].chapterTitle.has_value());
    assert(!pages[0].lines.empty());
    assert(pages[0].lines[0].heading);

    // Every continuation page has NO chapter title and ZERO heading lines
    for (std::size_t pi = 1; pi < pages.size(); ++pi) {
      assert(!pages[pi].chapterTitle.has_value());
      assert(!pages[pi].lines.empty());
      assert(pages[pi].lines[0].y == 0);
      for (const auto &line : pages[pi].lines) {
        assert(!line.heading);
      }
    }

    if (ch + 1 < 3) {
      r.nextChapter();
    }
  }
}

void testReaderLazyPerChapterPagination() {
  TextRenderer text;
  auto err = text.init("assets/fonts/Lato-Regular.ttf", nullptr);
  assert(!err.has_value());

  reader::Reader r(longBook());
  r.ensureLayout(text, 700, 420, 22);
  assert(r.paginatedChapterCount() == 1);

  // Font change and rotation only repaginate the currently visible chapter
  r.ensureLayout(text, 700, 420, 34);
  assert(r.paginatedChapterCount() == 1);
  r.ensureLayout(text, 420, 700, 34);
  assert(r.paginatedChapterCount() == 1);

  // Entering chapter 2 paginates chapter 2 lazily
  r.nextChapter();
  assert((r.chapterNumber() == std::pair<std::size_t, std::size_t>{2, 3}));
  assert(r.paginatedChapterCount() == 2);

  // Entering chapter 3 paginates chapter 3 lazily
  r.nextChapter();
  assert((r.chapterNumber() == std::pair<std::size_t, std::size_t>{3, 3}));
  assert(r.paginatedChapterCount() == 3);

  // Font resize while in chapter 3 resets cache and repaginates ONLY chapter 3
  r.ensureLayout(text, 420, 700, 24);
  assert((r.chapterNumber() == std::pair<std::size_t, std::size_t>{3, 3}));
  assert(r.paginatedChapterCount() == 1);
}

void testTouchCoordinatesAndZones() {
  auto approx = [](float a, float b) { return std::abs(a - b) < 1e-4f; };

  // 0 deg: identity
  auto [x0, y0] = touch::physicalToLogical(0.2f, 0.8f, 0);
  assert(approx(x0, 0.2f) && approx(y0, 0.8f));

  // 90 deg CW: logical top (ly=0) is physical right (nx=1), logical right (lx=1) is physical bottom (ny=1)
  auto [x1, y1] = touch::physicalToLogical(0.95f, 0.1f, 1);
  assert(approx(x1, 0.1f) && approx(y1, 0.05f));

  // 180 deg
  auto [x2, y2] = touch::physicalToLogical(0.25f, 0.75f, 2);
  assert(approx(x2, 0.75f) && approx(y2, 0.25f));

  // 270 deg CW: logical top (ly=0) is physical left (nx=0)
  auto [x3, y3] = touch::physicalToLogical(0.05f, 0.9f, 3);
  assert(approx(x3, 0.1f) && approx(y3, 0.05f));

  auto sendFingerTap = [](touch::TouchTracker &tt, float nx, float ny,
                          uint8_t rot, bool inReader,
                          touch::LibraryLayout lib = {},
                          bool hudVisible = true) {
    SDL_Event down{};
    down.type = SDL_FINGERDOWN;
    down.tfinger.fingerId = 42;
    down.tfinger.x = nx;
    down.tfinger.y = ny;
    tt.handleEvent(down, rot, 1280, 720, inReader, lib, hudVisible);

    SDL_Event up{};
    up.type = SDL_FINGERUP;
    up.tfinger.fingerId = 42;
    up.tfinger.x = nx;
    up.tfinger.y = ny;
    return tt.handleEvent(up, rot, 1280, 720, inReader, lib, hudVisible);
  };

  touch::TouchTracker tt;
  // In Portrait (rotation=1), physical top-right (nx=0.96, ny=0.10) maps to logical (< Lib) top-left!
  assert(sendFingerTap(tt, 0.96f, 0.10f, 1, true).cmd == AppCommand::Back);
  // Physical right-bottom (nx=0.96, ny=0.95) maps to logical top-right (Flip)
  assert(sendFingerTap(tt, 0.96f, 0.95f, 1, true).cmd == AppCommand::Rotate);
  // Physical right (nx=0.96, ny=0.80) maps to logical Theme
  assert(sendFingerTap(tt, 0.96f, 0.80f, 1, true).cmd == AppCommand::CycleTheme);
  // Physical right (nx=0.96, ny=0.69) maps to logical Sleep timer
  assert(sendFingerTap(tt, 0.96f, 0.69f, 1, true).cmd ==
         AppCommand::CycleSleepTimer);
  // Physical right (nx=0.96, ny=0.60) maps to logical A+
  assert(sendFingerTap(tt, 0.96f, 0.60f, 1, true).cmd == AppCommand::FontIncrease);
  // Physical right (nx=0.96, ny=0.50) maps to logical A-
  assert(sendFingerTap(tt, 0.96f, 0.50f, 1, true).cmd == AppCommand::FontDecrease);
  // Bottom-left in reader (HUD visible) cycles line spacing and margins
  assert(sendFingerTap(tt, 0.08f, 0.95f, 0, true).cmd ==
         AppCommand::CycleLineSpacing);
  assert(sendFingerTap(tt, 0.20f, 0.95f, 0, true).cmd ==
         AppCommand::CycleMargins);
  // Bottom-right in reader (HUD visible) cycles brightness
  assert(sendFingerTap(tt, 0.89f, 0.95f, 0, true).cmd ==
         AppCommand::CycleBrightness);

  // Center tap toggles HUD
  assert(sendFingerTap(tt, 0.50f, 0.50f, 0, true).cmd == AppCommand::ToggleHud);
  assert(sendFingerTap(tt, 0.50f, 0.50f, 1, true, {}, false).cmd ==
         AppCommand::ToggleHud);

  // When HUD is hidden, top-bar & bottom-bar button zones are ignored so
  // stray edge taps act as page turns instead of triggering hidden buttons.
  assert(sendFingerTap(tt, 0.96f, 0.10f, 1, true, {}, false).cmd ==
         AppCommand::PrevPage);
  assert(sendFingerTap(tt, 0.08f, 0.95f, 0, true, {}, false).cmd ==
         AppCommand::PrevPage);
  assert(sendFingerTap(tt, 0.89f, 0.95f, 0, true, {}, false).cmd ==
         AppCommand::NextPage);

  // Library top-bar supports Sleep timer and Brightness tap
  assert(sendFingerTap(tt, 0.96f, 0.69f, 1, false).cmd ==
         AppCommand::CycleSleepTimer);
  assert(sendFingerTap(tt, 0.96f, 0.59f, 1, false).cmd ==
         AppCommand::CycleBrightness);

  // Portrait reading body: logical left 1/3 (ny=0.15) -> PrevPage, right 2/3 (ny=0.70) -> NextPage
  assert(sendFingerTap(tt, 0.50f, 0.15f, 1, true).cmd == AppCommand::PrevPage);
  assert(sendFingerTap(tt, 0.50f, 0.70f, 1, true).cmd == AppCommand::NextPage);

  // Horizontal swipe in Portrait (logical dx < 0 means ny decreases -> NextPage)
  SDL_Event swDown{};
  swDown.type = SDL_FINGERDOWN;
  swDown.tfinger.fingerId = 7;
  swDown.tfinger.x = 0.50f;
  swDown.tfinger.y = 0.70f;
  tt.handleEvent(swDown, 1, 1280, 720, true, {});
  SDL_Event swUp{};
  swUp.type = SDL_FINGERUP;
  swUp.tfinger.fingerId = 7;
  swUp.tfinger.x = 0.50f;
  swUp.tfinger.y = 0.25f;
  assert(tt.handleEvent(swUp, 1, 1280, 720, true, {}).cmd ==
         AppCommand::NextPage);

  // Synthetic SDL_TOUCH_MOUSEID mouse events must be ignored
  SDL_Event fakeMouse{};
  fakeMouse.type = SDL_MOUSEBUTTONDOWN;
  fakeMouse.button.button = SDL_BUTTON_LEFT;
  fakeMouse.button.which = SDL_TOUCH_MOUSEID;
  fakeMouse.button.x = 900;
  fakeMouse.button.y = 400;
  assert(tt.handleEvent(fakeMouse, 0, 1280, 720, true, {}).cmd ==
         AppCommand::None);
  fakeMouse.type = SDL_MOUSEBUTTONUP;
  assert(tt.handleEvent(fakeMouse, 0, 1280, 720, true, {}).cmd ==
         AppCommand::None);

  // Library row tap in landscape: LIST_TOP=110, ROW_H=84 -> second row center is y=236
  touch::LibraryLayout lib{720, 4, 0};
  auto res = sendFingerTap(tt, 0.4f, 236.0f / 720.0f, 0, false, lib);
  assert(res.libraryRow.has_value());
  assert(*res.libraryRow == 1);
}

std::vector<uint8_t> renderHeadless(uint8_t rotation, int w, int h,
                                    int brightness = 100, bool showHud = true) {
  SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");
  assert(SDL_Init(SDL_INIT_VIDEO) == 0);

  SDL_Window *win = SDL_CreateWindow("t", 0, 0, w, h, 0);
  assert(win != nullptr);
  SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
  assert(ren != nullptr);

  TextRenderer text;
  auto err = text.init("assets/fonts/Lato-Regular.ttf", ren,
                       "assets/fonts/Lato-RegularItalic.ttf");
  assert(!err.has_value());

  std::string loadErr;
  auto book = epub::load("books/Alice_in_Wonderland.epub", loadErr);
  assert(book.has_value());
  reader::Reader reader(std::move(*book));

  int lw = (rotation % 2 == 1) ? h : w;
  int lh = (rotation % 2 == 1) ? w : h;
  SDL_Rect area = ui::readerArea(lw, lh);
  reader.ensureLayout(text, area.w, area.h, 22);

  const Theme theme = theme::applyBrightness(theme::get(0), brightness);
  SDL_Texture *target = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA8888,
                                          SDL_TEXTUREACCESS_TARGET, lw, lh);
  assert(target != nullptr);

  SDL_SetRenderTarget(ren, target);
  ui::drawReader(ren, text, theme, reader, lw, lh, DEFAULT_MARGIN_PERCENT, "5m",
                 std::to_string(brightness) + "%", "1.2x", "M:7%", showHud,
                 nullptr);
  SDL_SetRenderTarget(ren, nullptr);

  SDL_SetRenderDrawColor(ren, theme.bg.r, theme.bg.g, theme.bg.b, theme.bg.a);
  SDL_RenderClear(ren);
  SDL_Rect dest{(w - lw) / 2, (h - lh) / 2, lw, lh};
  SDL_RenderCopyEx(ren, target, nullptr, &dest,
                   static_cast<double>(rotation) * 90.0, nullptr, SDL_FLIP_NONE);

  std::vector<uint8_t> pixels(static_cast<std::size_t>(w) * h * 4);
  assert(SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_RGBA32,
                              pixels.data(), w * 4) == 0);

  SDL_DestroyTexture(target);
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  SDL_Quit();
  return pixels;
}

bool isLit(const uint8_t *px) { return px[0] > 0 || px[1] > 0 || px[2] > 0; }

void testOledThemeAndRotationRender() {
  const int w = 640;
  const int h = 360;
  auto landscape = renderHeadless(0, w, h);

  std::size_t litCount = 0;
  bool monochrome = true;
  bool hasFullWhite = false;
  for (std::size_t i = 0; i < landscape.size(); i += 4) {
    if (isLit(&landscape[i])) {
      ++litCount;
    }
    if (!(landscape[i] == landscape[i + 1] &&
          landscape[i + 1] == landscape[i + 2])) {
      monochrome = false;
    }
    if (landscape[i] == 255) {
      hasFullWhite = true;
    }
  }
  assert(litCount > 500);
  assert(litCount < static_cast<std::size_t>(w * h / 4));
  assert(monochrome);
  assert(hasFullWhite);

  for (auto [x, y] : std::vector<std::pair<int, int>>{
           {0, 0}, {w - 1, 0}, {0, h - 1}, {w - 1, h - 1}}) {
    std::size_t idx = static_cast<std::size_t>(y * w + x) * 4;
    assert(landscape[idx] == 0 && landscape[idx + 1] == 0 &&
           landscape[idx + 2] == 0);
  }

  auto portrait = renderHeadless(1, w, h);
  assert(landscape != portrait);

  auto colLit = [&](const std::vector<uint8_t> &px, int x) {
    for (int y = 0; y < h; ++y) {
      if (isLit(&px[static_cast<std::size_t>(y * w + x) * 4])) {
        return true;
      }
    }
    return false;
  };
  auto rowLit = [&](const std::vector<uint8_t> &px, int y) {
    for (int x = 0; x < w; ++x) {
      if (isLit(&px[static_cast<std::size_t>(y * w + x) * 4])) {
        return true;
      }
    }
    return false;
  };

  assert(!rowLit(landscape, 0));
  bool headerOnRight = false;
  for (int x = w - 40; x < w - 15; ++x) {
    if (colLit(portrait, x)) {
      headerOnRight = true;
      break;
    }
  }
  assert(headerOnRight);
  assert(!colLit(portrait, w - 1));
  assert(!rowLit(portrait, 0));
}

void testSleepGuardLifecycle() {
  using namespace sleep_guard;
  std::vector<bool> calls;
  TimePoint t0 = Clock::now();

  {
    SleepGuard guard([&](bool awake) { calls.push_back(awake); }, t0);
    assert(!guard.isActive());

    // Activates once within the 5-minute window
    guard.update(5, t0 + std::chrono::seconds(10));
    assert(guard.isActive());
    assert(calls.size() == 1 && calls[0] == true);

    // Subsequent updates inside the window must not spam the backend
    guard.update(5, t0 + std::chrono::minutes(4));
    assert(guard.isActive());
    assert(calls.size() == 1);

    // Once 5 minutes pass with no deliberate activity, wake-lock turns off
    guard.update(5, t0 + std::chrono::minutes(5));
    assert(!guard.isActive());
    assert(calls.size() == 2 && calls[1] == false);

    // Deliberate input re-arms the wake-lock for another 5 minutes
    guard.noteActivity(t0 + std::chrono::minutes(6));
    guard.update(5, t0 + std::chrono::minutes(7));
    assert(guard.isActive());
    assert(calls.size() == 3 && calls[2] == true);

    // Selecting "Sys" (0 minutes) disables the wake-lock immediately
    guard.update(0, t0 + std::chrono::minutes(7));
    assert(!guard.isActive());
    assert(calls.size() == 4 && calls[3] == false);

    // Re-arm before scope exit to verify destructor releases active wake-lock
    guard.noteActivity(t0 + std::chrono::minutes(8));
    guard.update(5, t0 + std::chrono::minutes(8));
    assert(guard.isActive());
    assert(calls.size() == 5 && calls[4] == true);
  }
  assert(calls.size() == 6 && calls[5] == false);

  // Joy-Con analog stick drift must NOT count as deliberate user activity,
  // whereas button/key/touch events must.
  SDL_Event drift{};
  drift.type = SDL_JOYAXISMOTION;
  assert(!isUserActivity(drift));

  SDL_Event key{};
  key.type = SDL_KEYDOWN;
  assert(isUserActivity(key));

  SDL_Event touch{};
  touch.type = SDL_FINGERDOWN;
  assert(isUserActivity(touch));

  SDL_Event joyBtn{};
  joyBtn.type = SDL_JOYBUTTONDOWN;
  assert(isUserActivity(joyBtn));
}

void testBrightnessAndHiddenHudRender() {
  Theme dimmed30 = theme::applyBrightness(theme::get(0), 30);
  assert(dimmed30.bg.r == 0 && dimmed30.bg.g == 0 && dimmed30.bg.b == 0);
  assert(dimmed30.text.r == 77 && dimmed30.text.g == 77 &&
         dimmed30.text.b == 77);

  const int w = 640;
  const int h = 360;
  auto pixels = renderHeadless(0, w, h, 50, false);

  uint8_t maxChannel = 0;
  std::size_t litCount = 0;
  for (std::size_t i = 0; i < pixels.size(); i += 4) {
    assert(pixels[i] == pixels[i + 1] && pixels[i + 1] == pixels[i + 2]);
    if (pixels[i] > maxChannel) {
      maxChannel = pixels[i];
    }
    if (isLit(&pixels[i])) {
      ++litCount;
    }
  }
  // Body text at 50% brightness peaks around 128 (never full 255)
  assert(litCount > 300);
  assert(maxChannel >= 120 && maxChannel <= 135);

  // Top header strip and bottom footer strip must remain completely unlit (pure #000)
  for (int y = 0; y < 55; ++y) {
    for (int x = 0; x < w; ++x) {
      assert(!isLit(&pixels[static_cast<std::size_t>(y * w + x) * 4]));
    }
  }
  for (int y = h - 45; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      assert(!isLit(&pixels[static_cast<std::size_t>(y * w + x) * 4]));
    }
  }
}

void writeStoredZip(const fs::path &path,
                    const std::vector<std::pair<std::string, std::string>> &files) {
  std::vector<uint8_t> out;
  auto pushU16 = [&](uint16_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  };
  auto pushU32 = [&](uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
  };

  struct Rec {
    std::string name;
    uint32_t size;
    uint32_t offset;
  };
  std::vector<Rec> recs;

  for (const auto &[name, body] : files) {
    uint32_t off = static_cast<uint32_t>(out.size());
    recs.push_back({name, static_cast<uint32_t>(body.size()), off});
    pushU32(0x04034b50u);
    pushU16(20);
    pushU16(0);
    pushU16(0); // stored
    pushU16(0);
    pushU16(0);
    pushU32(0);
    pushU32(static_cast<uint32_t>(body.size()));
    pushU32(static_cast<uint32_t>(body.size()));
    pushU16(static_cast<uint16_t>(name.size()));
    pushU16(0);
    out.insert(out.end(), name.begin(), name.end());
    out.insert(out.end(), body.begin(), body.end());
  }

  uint32_t cdOffset = static_cast<uint32_t>(out.size());
  for (const auto &r : recs) {
    pushU32(0x02014b50u);
    pushU16(20);
    pushU16(20);
    pushU16(0);
    pushU16(0);
    pushU16(0);
    pushU16(0);
    pushU32(0);
    pushU32(r.size);
    pushU32(r.size);
    pushU16(static_cast<uint16_t>(r.name.size()));
    pushU16(0);
    pushU16(0);
    pushU16(0);
    pushU16(0);
    pushU32(0);
    pushU32(r.offset);
    out.insert(out.end(), r.name.begin(), r.name.end());
  }
  uint32_t cdSize = static_cast<uint32_t>(out.size()) - cdOffset;

  pushU32(0x06054b50u);
  pushU16(0);
  pushU16(0);
  pushU16(static_cast<uint16_t>(recs.size()));
  pushU16(static_cast<uint16_t>(recs.size()));
  pushU32(cdSize);
  pushU32(cdOffset);
  pushU16(0);

  std::ofstream f(path, std::ios::binary);
  f.write(reinterpret_cast<const char *>(out.data()),
          static_cast<std::streamsize>(out.size()));
}

void testEpubMessyPathsAndInlineStyles() {
  assert(epub::urlDecode("My%20Book%2FPart%201.xhtml") ==
         "My Book/Part 1.xhtml");
  assert(epub::resolveZipPath("OEBPS/sub", "../Text/Ch%201.xhtml#sec2") ==
         "OEBPS/Text/Ch 1.xhtml");
  assert(epub::resolveZipPath("OEBPS", "./chap%202.html?v=1") ==
         "OEBPS/chap 2.html");

  // Inline styles & XML comment skipping
  auto blocks = epub::htmlToBlocks(
      "<html><body>"
      "<!-- <p>hidden comment</p> -->"
      "<p>Hello <em>very  important</em> and <strong>bold <i>both</i></strong>!</p>"
      "</body></html>");
  assert(blocks.size() == 1);
  const auto &b = blocks[0];
  assert(b.text == "Hello very important and bold both!");
  assert(b.spans.size() == 3);
  assert(b.text.substr(b.spans[0].start, b.spans[0].end - b.spans[0].start) ==
         "very important");
  assert(b.spans[0].style == epub::StyleItalic);
  assert(b.text.substr(b.spans[1].start, b.spans[1].end - b.spans[1].start) ==
         "bold ");
  assert(b.spans[1].style == epub::StyleBold);
  assert(b.text.substr(b.spans[2].start, b.spans[2].end - b.spans[2].start) ==
         "both");
  assert(b.spans[2].style == (epub::StyleBold | epub::StyleItalic));

  // Real messy EPUB archive: %20 OPF path, ../ relative spine href with #fragment
  fs::path dir = tempDir("messy-epub");
  fs::create_directories(dir);
  fs::path epubPath = dir / "messy.epub";
  writeStoredZip(
      epubPath,
      {
          {"META-INF/container.xml",
           "<container><!-- <rootfile full-path=\"bad.opf\"/> -->"
           "<rootfiles><rootfile full-path=\"My%20Book/sub/content.opf\"/>"
           "</rootfiles></container>"},
          {"My Book/sub/content.opf",
           "<package><metadata><dc:title>Messy &amp; Real</dc:title>"
           "<dc:creator>Jane Doe</dc:creator></metadata>"
           "<manifest><item id=\"c1\" href=\"../Text/Chapter%201.xhtml\"/></manifest>"
           "<spine><itemref idref=\"c1\"/></spine></package>"},
          {"My Book/Text/Chapter 1.xhtml",
           "<html><body><h1>Chapter <em>One</em></h1>"
           "<p>She whispered <b>quietly</b>.</p></body></html>"},
      });

  std::string err;
  auto loaded = epub::load(epubPath.string(), err);
  assert(loaded.has_value());
  assert(loaded->title == "Messy & Real");
  assert(loaded->author == "Jane Doe");
  assert(loaded->blocks.size() == 2);
  assert(loaded->blocks[0].text == "Chapter One");
  assert(loaded->blocks[0].spans.size() == 1);
  assert(loaded->blocks[0].spans[0].style == epub::StyleItalic);
  assert(loaded->blocks[1].text == "She whispered quietly.");
  assert(loaded->blocks[1].spans.size() == 1);
  assert(loaded->blocks[1].spans[0].style == epub::StyleBold);

  std::error_code ec;
  fs::remove_all(dir, ec);
}

void testTextKerningAndStyledMetrics() {
  TextRenderer text;
  auto err = text.init("assets/fonts/Lato-Regular.ttf", nullptr,
                       "assets/fonts/Lato-RegularItalic.ttf");
  assert(!err.has_value());

  int kernAV = text.getKerning('A', 'V', 36);
  assert(kernAV < 0);

  auto [wAV, _h] = text.measureText("AV", 36);
  auto [wA, _hA] = text.measureText("A", 36);
  auto [wV, _hV] = text.measureText("V", 36);
  assert(wAV < wA + wV);

  // Styled wrapping preserves bold/italic spans across wrapped lines
  std::string sample = "Normal text followed by emphasized words and bold endings";
  std::vector<epub::StyleSpan> spans = {
      {24, 40, epub::StyleItalic}, // "emphasized words"
      {45, 49, epub::StyleBold},   // "bold"
  };
  auto [lines, lh] = text.wrapLines(sample, 22, 220, spans);
  assert(lh > 0 && lines.size() >= 2);
  bool sawItalic = false;
  bool sawBold = false;
  for (const auto &l : lines) {
    for (const auto &sp : l.spans) {
      assert(sp.end <= l.text.size());
      if (sp.style & epub::StyleItalic) {
        sawItalic = true;
      }
      if (sp.style & epub::StyleBold) {
        sawBold = true;
      }
    }
  }
  assert(sawItalic && sawBold);
}

void testReaderLineSpacingAndMarginsPreserveBookmark() {
  TextRenderer text;
  auto err = text.init("assets/fonts/Lato-Regular.ttf", nullptr,
                       "assets/fonts/Lato-RegularItalic.ttf");
  assert(!err.has_value());

  reader::Reader r(longBook());
  SDL_Rect normalArea = ui::readerArea(720, 1280, 7);
  r.ensureLayout(text, normalArea.w, normalArea.h, 26, 120);
  std::size_t normalPages = r.activeChapterPageCount();

  for (int i = 0; i < 4; ++i) {
    r.nextPage();
  }
  auto spot = r.anchor();
  assert(spot.has_value());
  assert(pageContains(r, *spot));

  // Wider line spacing (170%) increases page count while keeping exact bookmark
  r.ensureLayout(text, normalArea.w, normalArea.h, 26, 170);
  assert(r.activeChapterPageCount() > normalPages);
  assert(r.anchor() == spot);
  assert(pageContains(r, *spot));
  assert(r.paginatedChapterCount() == 1);

  // Wide margins (18%) shrink readable width and increase page count, preserving bookmark
  SDL_Rect wideMarginArea = ui::readerArea(720, 1280, 18);
  assert(wideMarginArea.w < normalArea.w);
  r.ensureLayout(text, wideMarginArea.w, wideMarginArea.h, 26, 120);
  assert(r.activeChapterPageCount() > normalPages);
  assert(r.anchor() == spot);
  assert(pageContains(r, *spot));
  assert(r.paginatedChapterCount() == 1);
}

epub::Book multiChapterBook(std::size_t count) {
  epub::Book book;
  book.title = "Many Chapters";
  book.author = "Tester";
  for (std::size_t ch = 0; ch < count; ++ch) {
    book.chapterStarts.push_back(book.blocks.size());
    if (ch != 3) {
      book.blocks.push_back(epub::Block{
          epub::BlockKind::Heading, "Chapter " + std::to_string(ch + 1), {}});
    }
    for (int p = 0; p < 6; ++p) {
      book.blocks.push_back(epub::Block{
          epub::BlockKind::Paragraph,
          "Sample body text for chapter " + std::to_string(ch + 1) +
              " paragraph " + std::to_string(p) +
              ". Reading across multiple chapters should jump lazily.",
          {}});
    }
  }
  return book;
}

void testReaderGotoChapterAndTocMetadata() {
  TextRenderer text;
  auto err = text.init("assets/fonts/Lato-Regular.ttf", nullptr,
                       "assets/fonts/Lato-RegularItalic.ttf");
  assert(!err.has_value());

  epub::Book book = multiChapterBook(12);
  reader::Reader r(book);
  assert(r.chapterCount() == 12);
  assert(r.chapterDisplayTitle(0) == "Chapter 1");
  assert(r.chapterDisplayTitle(3) == "Chapter 4"); // Fallback when no heading

  uint32_t prevPct = 0;
  assert(r.chapterPercent(0) == 0);
  for (std::size_t i = 0; i < r.chapterCount(); ++i) {
    uint32_t pct = r.chapterPercent(i);
    assert(pct >= prevPct);
    prevPct = pct;
  }
  assert(r.chapterPercent(11) > 80 && r.chapterPercent(11) < 100);

  r.ensureLayout(text, 700, 420, 22);
  assert(r.paginatedChapterCount() == 1);

  // Direct jump to chapter 11 (index 10) lazily paginates only destination chapter
  r.gotoChapter(10);
  assert(r.currentChapterIndex() == 10);
  assert((r.chapterNumber() == std::pair<std::size_t, std::size_t>{11, 12}));
  assert(r.currentPageInChapter() == 0);
  assert(r.paginatedChapterCount() == 2);
  assert(r.page() != nullptr && r.page()->chapterTitle == "Chapter 11");
  assert(r.anchor().has_value());
  assert(pageContains(r, *r.anchor()));

  // Out-of-bounds jump is a safe no-op
  r.gotoChapter(999);
  assert(r.currentChapterIndex() == 10);

  // Jumping before layout preserves destination chapter once laid out
  reader::Reader r2(book);
  r2.gotoChapter(4);
  r2.ensureLayout(text, 700, 420, 22);
  assert(r2.currentChapterIndex() == 4);
  assert(r2.currentPageInChapter() == 0);
  assert(r2.paginatedChapterCount() == 1);
}

void testTouchTocZones() {
  // Scroll window clamping edge cases
  assert(ui::clampTocTop(5, 0, 0, 8) == 0);
  assert(ui::clampTocTop(2, 1, 5, 8) == 0);
  assert(ui::clampTocTop(10, 4, 20, 8) == 4);
  assert(ui::clampTocTop(0, 12, 20, 8) == 5);
  assert(ui::clampTocTop(99, 19, 20, 8) == 12);

  auto sendTapToc = [](touch::TouchTracker &tt, float nx, float ny, uint8_t rot,
                       bool inReader, bool hudVisible, touch::TocLayout toc) {
    SDL_Event down{};
    down.type = SDL_FINGERDOWN;
    down.tfinger.fingerId = 9;
    down.tfinger.x = nx;
    down.tfinger.y = ny;
    tt.handleEvent(down, rot, 1280, 720, inReader, {}, hudVisible, toc);

    SDL_Event up{};
    up.type = SDL_FINGERUP;
    up.tfinger.fingerId = 9;
    up.tfinger.x = nx;
    up.tfinger.y = ny;
    return tt.handleEvent(up, rot, 1280, 720, inReader, {}, hudVisible, toc);
  };

  auto sendSwipeToc = [](touch::TouchTracker &tt, float x0, float y0, float x1,
                         float y1, uint8_t rot, touch::TocLayout toc) {
    SDL_Event down{};
    down.type = SDL_FINGERDOWN;
    down.tfinger.fingerId = 10;
    down.tfinger.x = x0;
    down.tfinger.y = y0;
    tt.handleEvent(down, rot, 1280, 720, true, {}, true, toc);

    SDL_Event up{};
    up.type = SDL_FINGERUP;
    up.tfinger.fingerId = 10;
    up.tfinger.x = x1;
    up.tfinger.y = y1;
    return tt.handleEvent(up, rot, 1280, 720, true, {}, true, toc);
  };

  touch::TouchTracker tt;

  // Footer center tap opens TOC when HUD is visible (landscape and portrait)
  assert(sendTapToc(tt, 0.50f, 0.95f, 0, true, true, {}).cmd ==
         AppCommand::ToggleToc);
  assert(sendTapToc(tt, 0.05f, 0.50f, 1, true, true, {}).cmd ==
         AppCommand::ToggleToc);

  // With HUD hidden, tapping footer turns page instead of opening TOC
  assert(sendTapToc(tt, 0.50f, 0.95f, 0, true, false, {}).cmd ==
         AppCommand::NextPage);

  touch::TocLayout toc{true, 720, 25, 12, 8};
  // Top bar controls inside TOC
  assert(sendTapToc(tt, 0.10f, 0.04f, 0, true, true, toc).cmd ==
         AppCommand::Back);
  assert(sendTapToc(tt, 0.93f, 0.04f, 0, true, true, toc).cmd ==
         AppCommand::Rotate);

  // Tapping 3rd visible row (scrollTop=8 -> chapter index 10) in Landscape
  float row2Y =
      static_cast<float>(ui::TOC_LIST_TOP + 2 * ui::TOC_ROW_H +
                         ui::TOC_ROW_H / 2) /
      720.0f;
  auto tapRow = sendTapToc(tt, 0.40f, row2Y, 0, true, true, toc);
  assert(tapRow.tocRow.has_value() && *tapRow.tocRow == 10);

  // Tapping 3rd visible row in Portrait (rot=1, logicalHeight=1280 -> visible=19,
  // maxTop=6, so scrollTop=8 clamps to first=6 -> 3rd visible row is chapter 8)
  touch::TocLayout tocPortrait{true, 1280, 25, 12, 8};
  float lyP =
      static_cast<float>(ui::TOC_LIST_TOP + 2 * ui::TOC_ROW_H +
                         ui::TOC_ROW_H / 2) /
      1280.0f;
  auto tapRowP = sendTapToc(tt, 1.0f - lyP, 0.40f, 1, true, true, tocPortrait);
  assert(tapRowP.tocRow.has_value() && *tapRowP.tocRow == 8);

  // Vertical swipe scrolls TOC; horizontal swipe does nothing
  assert(sendSwipeToc(tt, 0.50f, 0.70f, 0.50f, 0.40f, 0, toc).cmd ==
         AppCommand::NextPage);
  assert(sendSwipeToc(tt, 0.50f, 0.40f, 0.50f, 0.70f, 0, toc).cmd ==
         AppCommand::PrevPage);
  auto horiz = sendSwipeToc(tt, 0.70f, 0.50f, 0.45f, 0.50f, 0, toc);
  assert(horiz.cmd == AppCommand::None && !horiz.tocRow.has_value());

  // Tapping below last chapter in a 3-chapter book is a no-op
  touch::TocLayout smallToc{true, 720, 3, 0, 0};
  float emptyRowY =
      static_cast<float>(ui::TOC_LIST_TOP + 5 * ui::TOC_ROW_H + 10) / 720.0f;
  auto emptyTap = sendTapToc(tt, 0.40f, emptyRowY, 0, true, true, smallToc);
  assert(emptyTap.cmd == AppCommand::None && !emptyTap.tocRow.has_value());

  // Comic mode touch zones (isComic=true) map [RTL]/[LTR] and [Page X/Y]
  auto sendComicTap = [&](float nx, float ny) {
    SDL_Event down{};
    down.type = SDL_FINGERDOWN;
    down.tfinger.fingerId = 11;
    down.tfinger.x = nx;
    down.tfinger.y = ny;
    tt.handleEvent(down, 0, 1280, 720, true, {}, true, {}, true);

    SDL_Event up{};
    up.type = SDL_FINGERUP;
    up.tfinger.fingerId = 11;
    up.tfinger.x = nx;
    up.tfinger.y = ny;
    return tt.handleEvent(up, 0, 1280, 720, true, {}, true, {}, true);
  };
  assert(sendComicTap(0.55f, 0.04f).cmd == AppCommand::ToggleReadingDirection);
  assert(sendComicTap(0.08f, 0.95f).cmd == AppCommand::ToggleReadingDirection);
  assert(sendComicTap(0.20f, 0.95f).cmd == AppCommand::CycleZoom);
  assert(sendComicTap(0.50f, 0.95f).cmd == AppCommand::ToggleToc);
  assert(sendComicTap(0.89f, 0.95f).cmd == AppCommand::CycleBrightness);

  // PDF touch zones map [Fit] in both top and bottom bar to CycleZoom
  SDL_Event pdfDown{}, pdfUp{};
  pdfDown.type = SDL_FINGERDOWN;
  pdfDown.tfinger.fingerId = 12;
  pdfDown.tfinger.x = 0.55f;
  pdfDown.tfinger.y = 0.04f;
  tt.handleEvent(pdfDown, 0, 1280, 720, true, {}, true, {}, false, true,
                 false);
  pdfUp.type = SDL_FINGERUP;
  pdfUp.tfinger.fingerId = 12;
  pdfUp.tfinger.x = 0.55f;
  pdfUp.tfinger.y = 0.04f;
  assert(tt.handleEvent(pdfUp, 0, 1280, 720, true, {}, true, {}, false, true,
                        false)
             .cmd == AppCommand::CycleZoom);

  // Zoomed pan drag produces panDx/panDy and does NOT trigger page turn on release
  SDL_Event panDown{}, panMove{}, panUp{};
  panDown.type = SDL_FINGERDOWN;
  panDown.tfinger.fingerId = 13;
  panDown.tfinger.x = 0.50f;
  panDown.tfinger.y = 0.50f;
  tt.handleEvent(panDown, 0, 1280, 720, true, {}, true, {}, true, false, true);

  panMove.type = SDL_FINGERMOTION;
  panMove.tfinger.fingerId = 13;
  panMove.tfinger.x = 0.40f;
  panMove.tfinger.y = 0.35f;
  auto moveRes = tt.handleEvent(panMove, 0, 1280, 720, true, {}, true, {}, true,
                                false, true);
  assert(moveRes.cmd == AppCommand::None);
  assert(std::abs(moveRes.panDx - 0.10f) < 1e-4f);
  assert(std::abs(moveRes.panDy - 0.15f) < 1e-4f);

  panUp.type = SDL_FINGERUP;
  panUp.tfinger.fingerId = 13;
  panUp.tfinger.x = 0.40f;
  panUp.tfinger.y = 0.35f;
  auto panUpRes = tt.handleEvent(panUp, 0, 1280, 720, true, {}, true, {}, true,
                                 false, true);
  assert(panUpRes.cmd == AppCommand::None);
}

std::vector<uint8_t> renderTocHeadless(uint8_t rotation, int w, int h) {
  SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");
  assert(SDL_Init(SDL_INIT_VIDEO) == 0);

  SDL_Window *win = SDL_CreateWindow("toc", 0, 0, w, h, 0);
  assert(win != nullptr);
  SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
  assert(ren != nullptr);

  TextRenderer text;
  auto err = text.init("assets/fonts/Lato-Regular.ttf", ren,
                       "assets/fonts/Lato-RegularItalic.ttf");
  assert(!err.has_value());

  reader::Reader reader(multiChapterBook(20));
  reader.gotoChapter(3);

  int lw = (rotation % 2 == 1) ? h : w;
  int lh = (rotation % 2 == 1) ? w : h;
  const Theme &theme = theme::get(0);
  SDL_Texture *target = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA8888,
                                          SDL_TEXTUREACCESS_TARGET, lw, lh);
  assert(target != nullptr);

  SDL_SetRenderTarget(ren, target);
  ui::drawToc(ren, text, theme, reader, 5, 2, lw, lh,
              "Up/Down select  |  Enter jump", nullptr);
  SDL_SetRenderTarget(ren, nullptr);

  SDL_SetRenderDrawColor(ren, theme.bg.r, theme.bg.g, theme.bg.b, theme.bg.a);
  SDL_RenderClear(ren);
  SDL_Rect dest{(w - lw) / 2, (h - lh) / 2, lw, lh};
  SDL_RenderCopyEx(ren, target, nullptr, &dest,
                   static_cast<double>(rotation) * 90.0, nullptr, SDL_FLIP_NONE);

  std::vector<uint8_t> pixels(static_cast<std::size_t>(w) * h * 4);
  assert(SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_RGBA32,
                              pixels.data(), w * 4) == 0);

  SDL_DestroyTexture(target);
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  SDL_Quit();
  return pixels;
}

void testTocHeadlessRender() {
  const int w = 640;
  const int h = 360;
  auto landscape = renderTocHeadless(0, w, h);

  std::size_t litCount = 0;
  bool monochrome = true;
  bool hasFullWhite = false;
  for (std::size_t i = 0; i < landscape.size(); i += 4) {
    if (isLit(&landscape[i])) {
      ++litCount;
    }
    if (!(landscape[i] == landscape[i + 1] &&
          landscape[i + 1] == landscape[i + 2])) {
      monochrome = false;
    }
    if (landscape[i] == 255) {
      hasFullWhite = true;
    }
  }
  assert(litCount > 300);
  assert(litCount < static_cast<std::size_t>(w * h / 4));
  assert(monochrome);
  assert(hasFullWhite);

  for (auto [x, y] : std::vector<std::pair<int, int>>{
           {0, 0}, {w - 1, 0}, {0, h - 1}, {w - 1, h - 1}}) {
    std::size_t idx = static_cast<std::size_t>(y * w + x) * 4;
    assert(landscape[idx] == 0 && landscape[idx + 1] == 0 &&
           landscape[idx + 2] == 0);
  }

  auto portrait = renderTocHeadless(1, w, h);
  assert(landscape != portrait);
}

void testZipStreamingReaderAndListing() {
  zip_util::ZipReader epubZip;
  std::string err;
  assert(epubZip.open("books/Alice_in_Wonderland.epub", err));
  assert(epubZip.hasEntry("META-INF/container.xml"));

  zip_util::ZipReader cbzZip;
  assert(cbzZip.open("books/Cyber_Manga_Sample.cbz", err));
  assert(cbzZip.entries().size() == 3);
  std::vector<uint8_t> jpgBytes;
  assert(cbzZip.readEntryBytes("page_01.jpg", jpgBytes, err));
  assert(jpgBytes.size() > 1000 && jpgBytes[0] == 0xFF && jpgBytes[1] == 0xD8);
}

void testCbzNaturalSortAndSampleLoad() {
  assert(cbz::naturalLess("page_2.jpg", "page_10.jpg"));
  assert(!cbz::naturalLess("page_10.jpg", "page_2.jpg"));
  assert(cbz::naturalLess("ch1/Page_002.png", "ch1/page_10.png"));

  std::string err;
  auto comic = cbz::ComicReader::open("books/Cyber_Manga_Sample.cbz", err);
  assert(comic.has_value());
  assert(comic->title() == "Cyber Manga Sample");
  assert(comic->pageCount() == 3);
  assert(comic->currentPageIndex() == 0);
  assert(comic->permille() == 0);

  cbz::DecodedImage img;
  assert(comic->decodePage(0, img, err));
  assert(img.width > 0 && img.height > 0);
  assert(img.rgba.size() ==
         static_cast<std::size_t>(img.width) * img.height * 4);

  comic->nextPage();
  assert(comic->currentPageIndex() == 1);
  assert(comic->permille() == 500);
  comic->nextPage();
  assert(comic->currentPageIndex() == 2);
  assert(comic->permille() == 1000);
  comic->nextPage(); // Clamp at end
  assert(comic->currentPageIndex() == 2);
  comic->jumpBy(-10);
  assert(comic->currentPageIndex() == 0);
  comic->gotoPage(1);
  assert(comic->currentPageIndex() == 1);

  auto saved = comic->anchor();
  assert(saved.has_value() && saved->first == 1);
  auto comic2 = cbz::ComicReader::open("books/Cyber_Manga_Sample.cbz", err);
  assert(comic2.has_value());
  comic2->restore(*saved);
  assert(comic2->currentPageIndex() == 1);
}

std::string makeMinimal1x1Bmp() {
  // Uncompressed 24-bit 1x1 white BMP (58 bytes)
  const uint8_t bmp[] = {
      0x42, 0x4D, 58, 0, 0, 0, 0, 0, 0, 0, 54, 0, 0, 0,
      40,   0,    0,  0, 1, 0, 0, 0, 1, 0, 0,  0, 1, 0, 24, 0,
      0,    0,    0,  0, 4, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0,  0,
      0,    0,    0,  0, 0, 0, 0, 0, 255, 255, 255, 0};
  return std::string(reinterpret_cast<const char *>(bmp), sizeof(bmp));
}

void testCbzLibraryScanAndComicInfo() {
  fs::path progDir = tempDir("cbz-library");
  auto entries = library::scan({"books"}, progDir.string());
  bool foundEpub = false;
  bool foundCbz = false;
  for (const auto &e : entries) {
    if (e.path.find("Alice_in_Wonderland.epub") != std::string::npos) {
      foundEpub = true;
      assert(e.format == library::Format::Epub);
    }
    if (e.path.find("Cyber_Manga_Sample.cbz") != std::string::npos) {
      foundCbz = true;
      assert(e.format == library::Format::Cbz);
      assert(e.author == "CBZ (3 pages)");
    }
  }
  assert(foundEpub && foundCbz);

  // Synthetic CBZ with ComicInfo.xml, __MACOSX junk, and unsorted page_10/page_2
  fs::path cbzDir = tempDir("synthetic-cbz");
  fs::create_directories(cbzDir);
  fs::path cbzPath = cbzDir / "issue.cbz";
  std::string bmp = makeMinimal1x1Bmp();
  writeStoredZip(
      cbzPath,
      {
          {"__MACOSX/._page_2.bmp", "junk"},
          {".DS_Store", "junk"},
          {"page_10.bmp", bmp},
          {"page_2.bmp", bmp},
          {"ComicInfo.xml",
           "<ComicInfo><Series>Cyber Ninja</Series><Number>7</Number>"
           "<Writer>K. Otomo</Writer><Manga>YesAndRightToLeft</Manga></ComicInfo>"},
      });

  std::string err;
  auto comic = cbz::ComicReader::open(cbzPath.string(), err);
  assert(comic.has_value());
  assert(comic->pageCount() == 2);
  assert(comic->title() == "Cyber Ninja #7");
  assert(comic->author() == "K. Otomo");
  assert(comic->mangaRtlHint() == std::optional<bool>(true));
  assert(comic->pageDisplayTitle(0) == "Page 1  (page_2.bmp)");
  assert(comic->pageDisplayTitle(1) == "Page 2  (page_10.bmp)");

  std::error_code ec;
  fs::remove_all(progDir, ec);
  fs::remove_all(cbzDir, ec);
}

void testCbzFitRectAndHeadlessRender() {
  SDL_Rect tallFit = cbz::fitRect(600, 1200, SDL_Rect{0, 60, 1280, 604});
  assert(tallFit.h == 604 && tallFit.w == 302 && tallFit.x == (1280 - 302) / 2 &&
         tallFit.y == 60);

  SDL_Rect wideFit = cbz::fitRect(1600, 800, SDL_Rect{0, 0, 720, 1280});
  assert(wideFit.w == 720 && wideFit.h == 360 && wideFit.x == 0 &&
         wideFit.y == (1280 - 360) / 2);

  SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");
  assert(SDL_Init(SDL_INIT_VIDEO) == 0);
  const int w = 640;
  const int h = 360;
  SDL_Window *win = SDL_CreateWindow("cbz", 0, 0, w, h, 0);
  assert(win != nullptr);
  SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
  assert(ren != nullptr);

  TextRenderer text;
  auto err = text.init("assets/fonts/Lato-Regular.ttf", ren,
                       "assets/fonts/Lato-RegularItalic.ttf");
  assert(!err.has_value());

  std::string cbzErr;
  auto comic = cbz::ComicReader::open("books/Cyber_Manga_Sample.cbz", cbzErr);
  assert(comic.has_value());

  ui::ComicTextureCache cache;
  int imgW = 0;
  int imgH = 0;
  SDL_Texture *tex = cache.get(ren, "books/Cyber_Manga_Sample.cbz", *comic,
                               imgW, imgH, cbzErr);
  assert(tex != nullptr && imgW > 0 && imgH > 0);

  auto renderFrame = [&](uint8_t rot, int brightness, bool hud) {
    int lw = (rot % 2 == 1) ? h : w;
    int lh = (rot % 2 == 1) ? w : h;
    const Theme theme = theme::applyBrightness(theme::get(0), brightness);
    SDL_Texture *target = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA8888,
                                            SDL_TEXTUREACCESS_TARGET, lw, lh);
    assert(target != nullptr);
    SDL_SetRenderTarget(ren, target);
    ui::drawComic(ren, text, theme, *comic, tex, imgW, imgH, lw, lh, brightness,
                  "RTL", "5m", std::to_string(brightness) + "%", hud, nullptr);
    SDL_SetRenderTarget(ren, nullptr);

    SDL_SetRenderDrawColor(ren, theme.bg.r, theme.bg.g, theme.bg.b, theme.bg.a);
    SDL_RenderClear(ren);
    SDL_Rect dest{(w - lw) / 2, (h - lh) / 2, lw, lh};
    SDL_RenderCopyEx(ren, target, nullptr, &dest, static_cast<double>(rot) * 90.0,
                     nullptr, SDL_FLIP_NONE);
    std::vector<uint8_t> px(static_cast<std::size_t>(w) * h * 4);
    assert(SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_RGBA32, px.data(),
                                w * 4) == 0);
    SDL_DestroyTexture(target);
    return px;
  };

  auto full = renderFrame(0, 100, true);
  auto portrait = renderFrame(1, 100, true);
  auto dimmed = renderFrame(0, 50, false);

  // Corner pixels stay pure OLED Black (#000) in letterboxed/pillarboxed areas
  for (auto [cx, cy] : std::vector<std::pair<int, int>>{
           {0, 0}, {w - 1, 0}, {0, h - 1}, {w - 1, h - 1}}) {
    std::size_t idx = static_cast<std::size_t>(cy * w + cx) * 4;
    assert(full[idx] == 0 && full[idx + 1] == 0 && full[idx + 2] == 0);
  }

  uint8_t maxFull = 0;
  uint8_t maxDim = 0;
  std::size_t litFull = 0;
  for (std::size_t i = 0; i < full.size(); i += 4) {
    maxFull = std::max({maxFull, full[i], full[i + 1], full[i + 2]});
    maxDim = std::max({maxDim, dimmed[i], dimmed[i + 1], dimmed[i + 2]});
    if (isLit(&full[i])) {
      ++litFull;
    }
  }
  assert(litFull > 5000);
  assert(maxFull > 200);
  assert(maxDim <= 135 && maxDim >= 80);
  assert(full != portrait);

  cache.clear();
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  SDL_Quit();
}

void testCbzWebpDecodeAndZoomPan() {
  // Encode a real 4x4 RGBA WebP bitstream and verify decodeImageBytes
  std::vector<uint8_t> rgba(4 * 4 * 4, 0);
  for (std::size_t i = 0; i < 16; ++i) {
    rgba[i * 4 + 0] = 200;
    rgba[i * 4 + 1] = 100;
    rgba[i * 4 + 2] = 50;
    rgba[i * 4 + 3] = 255;
  }
  uint8_t *webpBuf = nullptr;
  std::size_t webpSz =
      WebPEncodeLosslessRGBA(rgba.data(), 4, 4, 4 * 4, &webpBuf);
  assert(webpSz > 12 && webpBuf != nullptr);

  cbz::DecodedImage decoded;
  std::string err;
  assert(cbz::decodeImageBytes(webpBuf, webpSz, decoded, err));
  WebPFree(webpBuf);
  assert(decoded.width == 4 && decoded.height == 4);
  assert(decoded.rgba[0] == 200 && decoded.rgba[1] == 100 &&
         decoded.rgba[2] == 50 && decoded.rgba[3] == 255);

  // Zoom modes & pan clamping
  SDL_Rect bounds{0, 60, 800, 600};
  float px = 500.0f;
  float py = -500.0f;
  SDL_Rect fit =
      cbz::zoomedRect(600, 900, bounds, cbz::ZoomMode::FitPage, px, py);
  assert(fit.w == 400 && fit.h == 600);
  assert(px == 0.0f && py == 0.0f); // No overflow -> pan clamped to 0

  px = 9999.0f;
  py = -9999.0f;
  SDL_Rect fitW =
      cbz::zoomedRect(600, 900, bounds, cbz::ZoomMode::FitWidth, px, py);
  assert(fitW.w == 800 && fitW.h == 1200);
  assert(fitW.x == 0 && fitW.y == 60); // Clamped top-aligned
  assert(px == 0.0f && py == -300.0f);

  px = 9999.0f;
  py = 9999.0f;
  SDL_Rect z200 =
      cbz::zoomedRect(600, 900, bounds, cbz::ZoomMode::Zoom200, px, py);
  assert(z200.w == 800 && z200.h == 1200);
  assert(py == 300.0f);
}

void testEpubTocNcxNavAndCover() {
  fs::path dir = tempDir("epub-toc-cover");
  fs::create_directories(dir);
  std::string bmp = makeMinimal1x1Bmp();

  // Case 1: EPUB3 nav.xhtml + properties="cover-image"
  fs::path epub3 = dir / "book3.epub";
  writeStoredZip(
      epub3,
      {
          {"META-INF/container.xml",
           "<container><rootfiles>"
           "<rootfile full-path=\"OEBPS/content.opf\"/>"
           "</rootfiles></container>"},
          {"OEBPS/content.opf",
           "<package><metadata><dc:title>Nav Book</dc:title>"
           "<dc:creator>Author</dc:creator></metadata>"
           "<manifest>"
           "<item id=\"nav\" href=\"nav.xhtml\" properties=\"nav\" media-type=\"application/xhtml+xml\"/>"
           "<item id=\"cov\" href=\"images/cover.bmp\" properties=\"cover-image\" media-type=\"image/bmp\"/>"
           "<item id=\"c1\" href=\"text/ch1.xhtml\" media-type=\"application/xhtml+xml\"/>"
           "<item id=\"c2\" href=\"text/ch2.xhtml\" media-type=\"application/xhtml+xml\"/>"
           "</manifest>"
           "<spine><itemref idref=\"c1\"/><itemref idref=\"c2\"/></spine></package>"},
          {"OEBPS/nav.xhtml",
           "<html><body><nav epub:type=\"toc\"><ol>"
           "<li><a href=\"text/ch1.xhtml#top\">Prologue: Dawn</a></li>"
           "<li><a href=\"text/ch2.xhtml\">Act <em>One</em></a></li>"
           "</ol></nav></body></html>"},
          {"OEBPS/images/cover.bmp", bmp},
          {"OEBPS/text/ch1.xhtml", "<html><body><p>Opening paragraph without h1.</p></body></html>"},
          {"OEBPS/text/ch2.xhtml", "<html><body><p>Second chapter body.</p></body></html>"},
      });

  std::string err;
  auto b3 = epub::load(epub3.string(), err);
  assert(b3.has_value());
  assert(b3->chapters.size() == 2);
  assert(b3->chapters[0].title == "Prologue: Dawn");
  assert(b3->chapters[1].title == "Act One");

  std::vector<uint8_t> coverBytes;
  assert(epub::extractCoverImage(epub3.string(), coverBytes, err));
  cbz::DecodedImage coverImg;
  assert(cbz::decodeImageBytes(coverBytes.data(), coverBytes.size(), coverImg,
                               err));
  assert(coverImg.width == 1 && coverImg.height == 1);

  // Case 2: EPUB2 toc.ncx + <meta name="cover" content="..."/>
  fs::path epub2 = dir / "book2.epub";
  writeStoredZip(
      epub2,
      {
          {"META-INF/container.xml",
           "<container><rootfiles>"
           "<rootfile full-path=\"content.opf\"/>"
           "</rootfiles></container>"},
          {"content.opf",
           "<package><metadata><dc:title>NCX Book</dc:title>"
           "<meta content=\"mycov\" name=\"cover\"/>"
           "</metadata>"
           "<manifest>"
           "<item id=\"ncx\" href=\"toc.ncx\" media-type=\"application/x-dtbncx+xml\"/>"
           "<item id=\"mycov\" href=\"front.bmp\" media-type=\"image/bmp\"/>"
           "<item id=\"p1\" href=\"part1.xhtml\" media-type=\"application/xhtml+xml\"/>"
           "</manifest>"
           "<spine toc=\"ncx\"><itemref idref=\"p1\"/></spine></package>"},
          {"toc.ncx",
           "<ncx><navMap><navPoint id=\"np1\">"
           "<navLabel><text>Part I: Beginnings</text></navLabel>"
           "<content src=\"part1.xhtml#start\"/>"
           "</navPoint></navMap></ncx>"},
          {"front.bmp", bmp},
          {"part1.xhtml", "<html><body><p>Text only.</p></body></html>"},
      });

  auto b2 = epub::load(epub2.string(), err);
  assert(b2.has_value());
  assert(b2->chapters.size() == 1);
  assert(b2->chapters[0].title == "Part I: Beginnings");
  coverBytes.clear();
  assert(epub::extractCoverImage(epub2.string(), coverBytes, err));

  std::error_code ec;
  fs::remove_all(dir, ec);
}

void testPdfLoadsSampleAndRenders() {
  std::string err;
  auto doc = pdf::PdfReader::open("books/Sample_Handbook.pdf", err);
  assert(doc.has_value());
  assert(doc->pageCount() == 2);
  assert(doc->currentPageIndex() == 0);
  assert(doc->permille() == 0);

  cbz::DecodedImage p0;
  assert(doc->renderPage(0, 640, 800, p0, err));
  assert(p0.width > 100 && p0.height > 100);
  bool hasDarkInk = false;
  bool hasWhitePaper = false;
  for (std::size_t i = 0; i < p0.rgba.size(); i += 4) {
    if (p0.rgba[i] < 40 && p0.rgba[i + 1] < 40 && p0.rgba[i + 2] < 40) {
      hasDarkInk = true;
    }
    if (p0.rgba[i] > 240 && p0.rgba[i + 1] > 240 && p0.rgba[i + 2] > 240) {
      hasWhitePaper = true;
    }
  }
  assert(hasDarkInk && hasWhitePaper);

  doc->nextPage();
  assert(doc->currentPageIndex() == 1 && doc->permille() == 1000);
  doc->prevPage();
  assert(doc->currentPageIndex() == 0);
  doc->jumpBy(10);
  assert(doc->currentPageIndex() == 1);
  auto saved = doc->anchor();
  assert(saved.has_value() && saved->first == 1);

  auto doc2 = pdf::PdfReader::open("books/Sample_Handbook.pdf", err);
  assert(doc2.has_value());
  doc2->restore(*saved);
  assert(doc2->currentPageIndex() == 1);
}

void testLibraryScansAllThreeFormats() {
  fs::path progDir = tempDir("lib-all-formats");
  auto entries = library::scan({"books"}, progDir.string());
  bool hasEpub = false, hasCbz = false, hasPdf = false;
  for (const auto &e : entries) {
    if (e.format == library::Format::Epub &&
        e.path.find("Alice_in_Wonderland.epub") != std::string::npos) {
      hasEpub = true;
    }
    if (e.format == library::Format::Cbz &&
        e.path.find("Cyber_Manga_Sample.cbz") != std::string::npos) {
      hasCbz = true;
    }
    if (e.format == library::Format::Pdf &&
        e.path.find("Sample_Handbook.pdf") != std::string::npos) {
      hasPdf = true;
      assert(e.author == "PDF (2 pages)");
    }
  }
  assert(hasEpub && hasCbz && hasPdf);
  std::error_code ec;
  fs::remove_all(progDir, ec);
}

void testUiCoversAndPdfHeadlessRender() {
  SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");
  assert(SDL_Init(SDL_INIT_VIDEO) == 0);
  const int w = 640;
  const int h = 360;
  SDL_Window *win = SDL_CreateWindow("libpdf", 0, 0, w, h, 0);
  assert(win != nullptr);
  SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
  assert(ren != nullptr);

  TextRenderer text;
  auto err = text.init("assets/fonts/Lato-Regular.ttf", ren,
                       "assets/fonts/Lato-RegularItalic.ttf");
  assert(!err.has_value());

  fs::path progDir = tempDir("ui-covers");
  auto entries = library::scan({"books"}, progDir.string());
  ui::CoverCache covers;
  int cw = 0, ch = 0;
  for (const auto &e : entries) {
    if (e.format == library::Format::Cbz || e.format == library::Format::Pdf) {
      SDL_Texture *ct = covers.get(ren, e, cw, ch);
      assert(ct != nullptr && cw > 0 && ch > 0);
    }
  }

  SDL_Texture *target = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA8888,
                                          SDL_TEXTUREACCESS_TARGET, w, h);
  assert(target != nullptr);
  SDL_SetRenderTarget(ren, target);
  ui::drawLibrary(ren, text, theme::get(0), entries, 0, w, h, "Hint", "Empty",
                  "5m", "100%", nullptr, &covers, 100);
  SDL_SetRenderTarget(ren, nullptr);
  SDL_RenderCopy(ren, target, nullptr, nullptr);
  std::vector<uint8_t> libPx(static_cast<std::size_t>(w) * h * 4);
  assert(SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_RGBA32,
                              libPx.data(), w * 4) == 0);
  assert(libPx[0] == 0 && libPx[1] == 0 && libPx[2] == 0);

  // Render PDF zoomed with pan clamping & HUD clipping
  std::string pdfErr;
  auto doc = pdf::PdfReader::open("books/Sample_Handbook.pdf", pdfErr);
  assert(doc.has_value());
  ui::PageTextureCache pageCache;
  int pw = 0, ph = 0;
  SDL_Texture *pdfTex = pageCache.getPdf(ren, "books/Sample_Handbook.pdf",
                                         *doc, w, h, pw, ph, pdfErr);
  assert(pdfTex != nullptr && pw > 0 && ph > 0);
  pageCache.prefetchPdf("books/Sample_Handbook.pdf", 1, *doc, w, h);
  assert(pageCache.hasPrefetch && pageCache.prefetchIndex == 1);

  float panX = 0.0f, panY = -1000.0f;
  SDL_SetRenderTarget(ren, target);
  ui::drawVisualPage(ren, text, theme::get(0), doc->title(), doc->author(), 0,
                     doc->pageCount(), doc->permille(), pdfTex, pw, ph, w, h,
                     100, cbz::ZoomMode::Zoom150, panX, panY, nullptr, "5m",
                     "100%", true, nullptr);
  SDL_SetRenderTarget(ren, nullptr);

  pageCache.clear();
  covers.clear();
  SDL_DestroyTexture(target);
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  SDL_Quit();

  std::error_code ec;
  fs::remove_all(progDir, ec);
}

void testTextGlyphCacheBoundedAndInvalidate() {
  TextRenderer text;
  auto err = text.init("assets/fonts/Lato-Regular.ttf", nullptr,
                       "assets/fonts/Lato-RegularItalic.ttf");
  assert(!err.has_value());

  // Measure many (codepoint x fontSize) pairs to exceed MAX_GLYPH_CACHE
  for (int sz = 14; sz <= 56; sz += 2) {
    text.measureText(
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789!@#$%^&*()",
        sz);
  }
  assert(text.cachedGlyphCount() > 0);
  assert(text.cachedGlyphCount() <= TextRenderer::MAX_GLYPH_CACHE);

  text.invalidateTextures();
  assert(text.cachedGlyphCount() == 0);
  auto [w, h] = text.measureText("Hello", 24);
  assert(w > 0 && h > 0 && text.cachedGlyphCount() > 0);
}

void testZip64ArchiveSupport() {
  fs::path dir = tempDir("zip64-test");
  fs::create_directories(dir);
  fs::path zpath = dir / "test64.zip";

  std::vector<uint8_t> out;
  auto pushU16 = [&](uint16_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  };
  auto pushU32 = [&](uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
  };
  auto pushU64 = [&](uint64_t v) {
    pushU32(static_cast<uint32_t>(v & 0xFFFFFFFFu));
    pushU32(static_cast<uint32_t>((v >> 32) & 0xFFFFFFFFu));
  };

  std::string name = "big.txt";
  std::string body = "ZIP64 payload works cleanly!";
  // Local file header
  pushU32(0x04034b50u);
  pushU16(45);
  pushU16(0);
  pushU16(0);
  pushU16(0);
  pushU16(0);
  pushU32(0);
  pushU32(static_cast<uint32_t>(body.size()));
  pushU32(static_cast<uint32_t>(body.size()));
  pushU16(static_cast<uint16_t>(name.size()));
  pushU16(0);
  out.insert(out.end(), name.begin(), name.end());
  out.insert(out.end(), body.begin(), body.end());

  uint64_t cdOffset = out.size();
  // Central directory entry using 0xFFFFFFFF sentinels + ZIP64 extra 0x0001
  pushU32(0x02014b50u);
  pushU16(45);
  pushU16(45);
  pushU16(0);
  pushU16(0);
  pushU16(0);
  pushU16(0);
  pushU32(0);
  pushU32(0xFFFFFFFFu);
  pushU32(0xFFFFFFFFu);
  pushU16(static_cast<uint16_t>(name.size()));
  pushU16(28); // ZIP64 extra field length
  pushU16(0);
  pushU16(0);
  pushU16(0);
  pushU32(0);
  pushU32(0xFFFFFFFFu);
  out.insert(out.end(), name.begin(), name.end());
  pushU16(0x0001u);
  pushU16(24);
  pushU64(body.size());
  pushU64(body.size());
  pushU64(0); // local header offset
  uint64_t cdSize = out.size() - cdOffset;

  uint64_t eocd64Off = out.size();
  pushU32(0x06064b50u);
  pushU64(44);
  pushU16(45);
  pushU16(45);
  pushU32(0);
  pushU32(0);
  pushU64(1);
  pushU64(1);
  pushU64(cdSize);
  pushU64(cdOffset);

  // ZIP64 EOCD Locator
  pushU32(0x07064b50u);
  pushU32(0);
  pushU64(eocd64Off);
  pushU32(1);

  // Standard EOCD with saturated sentinels
  pushU32(0x06054b50u);
  pushU16(0);
  pushU16(0);
  pushU16(0xFFFFu);
  pushU16(0xFFFFu);
  pushU32(0xFFFFFFFFu);
  pushU32(0xFFFFFFFFu);
  pushU16(0);

  {
    std::ofstream f(zpath, std::ios::binary);
    f.write(reinterpret_cast<const char *>(out.data()),
            static_cast<std::streamsize>(out.size()));
  }

  zip_util::ZipReader zip;
  std::string err;
  assert(zip.open(zpath.string(), err));
  assert(zip.hasEntry("big.txt"));
  std::string readBack;
  assert(zip.readEntry("big.txt", readBack, err));
  assert(readBack == body);

  std::error_code ec;
  fs::remove_all(dir, ec);
}

void testEpubNamespacedNcxCommentsAndBr() {
  fs::path dir = tempDir("epub-ns-br");
  fs::create_directories(dir);
  fs::path ep = dir / "ns.epub";
  writeStoredZip(
      ep,
      {
          {"META-INF/container.xml",
           "<container><rootfiles>"
           "<rootfile full-path=\"content.opf\"/>"
           "</rootfiles></container>"},
          {"content.opf",
           "<opf:package><opf:metadata>"
           "<!-- <dc:title>Commented Fake Title</dc:title> -->"
           "<dc:title-sort>Ignored Sort Key</dc:title-sort>"
           "<dc:title>Real &amp; Clean Title</dc:title>"
           "<dc:creator>Real Author</dc:creator>"
           "</opf:metadata>"
           "<opf:manifest>"
           "<opf:item id=\"ncx\" href=\"toc.ncx\" media-type=\"application/x-dtbncx+xml\"/>"
           "<opf:item id=\"p1\" href=\"poem.xhtml\" media-type=\"application/xhtml+xml\"/>"
           "</opf:manifest>"
           "<opf:spine toc=\"ncx\"><opf:itemref idref=\"p1\"/></opf:spine></opf:package>"},
          {"toc.ncx",
           "<ncx:ncx><ncx:navMap><ncx:navPoint id=\"n1\">"
           "<ncx:navLabel><ncx:text>Verse &amp; Song</ncx:text></ncx:navLabel>"
           "<ncx:content src=\"poem.xhtml\"/>"
           "</ncx:navPoint></ncx:navMap></ncx:ncx>"},
          {"poem.xhtml",
           "<html><body><h4>Subhead</h4>"
           "<p>Line one<br/>Line two</p></body></html>"},
      });

  std::string err;
  auto loaded = epub::load(ep.string(), err);
  assert(loaded.has_value());
  assert(loaded->title == "Real & Clean Title");
  assert(loaded->author == "Real Author");
  assert(loaded->chapters.size() == 1);
  assert(loaded->chapters[0].title == "Verse & Song");
  assert(loaded->blocks.size() == 2);
  assert(loaded->blocks[0].kind == epub::BlockKind::Heading);
  assert(loaded->blocks[0].text == "Subhead");
  assert(loaded->blocks[1].kind == epub::BlockKind::Paragraph);
  assert(loaded->blocks[1].text == "Line one\nLine two");

  std::error_code ec;
  fs::remove_all(dir, ec);
}

void testProgressDistinctPathsSameBasename() {
  fs::path dir = tempDir("progress-collision");
  std::string p1 = "library/vol1/chapter.cbz";
  std::string p2 = "library/vol2/chapter.cbz";
  assert(progress::progressFile(dir.string(), p1) !=
         progress::progressFile(dir.string(), p2));

  std::string err;
  assert(progress::save(dir.string(), p1, {{3, 0}, 300}, err));
  assert(progress::save(dir.string(), p2, {{9, 0}, 900}, err));

  auto l1 = progress::load(dir.string(), p1);
  auto l2 = progress::load(dir.string(), p2);
  assert(l1.has_value() && l1->anchor.first == 3 && l1->permille == 300);
  assert(l2.has_value() && l2->anchor.first == 9 && l2->permille == 900);

  // Unhashed legacy .pos filename still loads when hashed file is absent
  std::string p3 = "library/vol3/legacy.epub";
  fs::create_directories(dir);
  {
    std::ofstream out(progress::legacyProgressFile(dir.string(), p3));
    out << "4 12 250\n";
  }
  auto l3 = progress::load(dir.string(), p3);
  assert(l3.has_value() && l3->anchor.first == 4 && l3->anchor.second == 12 &&
         l3->permille == 250);

  std::error_code ec;
  fs::remove_all(dir, ec);
}

void testCbzHasOverflowCheck() {
  SDL_Rect bounds{0, 60, 800, 600};
  // Wide image in FitWidth (800x400 inside 800x600) has zero overflow -> swipe still turns page
  assert(!cbz::hasOverflow(1600, 800, bounds, cbz::ZoomMode::FitPage));
  assert(!cbz::hasOverflow(1600, 800, bounds, cbz::ZoomMode::FitWidth));
  assert(cbz::hasOverflow(1600, 800, bounds, cbz::ZoomMode::Zoom150));

  // Tall manga page in FitWidth (800x1200 inside 800x600) overflows vertically -> drag pans
  assert(!cbz::hasOverflow(600, 1200, bounds, cbz::ZoomMode::FitPage));
  assert(cbz::hasOverflow(600, 1200, bounds, cbz::ZoomMode::FitWidth));
}

void testAppControllerRoutingAndSwitchRtl() {
  fs::path progDir = tempDir("app-ctrl");
  sleep_guard::SleepGuard guard([](bool) {});
  int lw = 1280, lh = 720, w = 1280, h = 720;

  // Test Switch config (Sconfig): X in CBZ toggles RTL/LTR
  Sconfig sc;
  AppController app;
  app.init(sc, {"books/Cyber_Manga_Sample.cbz"}, progDir.string());
  assert(app.current.has_value() && app.current->isComic());
  assert(app.settings.mangaRtl == true);
  assert(app.current->comic().currentPageIndex() == 0);

  // Under RTL: PrevPage advances forward, NextPage goes backward
  app.dispatchCommand(AppCommand::PrevPage, sc, guard, lw, lh, w, h);
  assert(app.current->comic().currentPageIndex() == 1);
  app.dispatchCommand(AppCommand::NextPage, sc, guard, lw, lh, w, h);
  assert(app.current->comic().currentPageIndex() == 0);

  // Switch X (CycleTheme) toggles reading direction to LTR in CBZ
  std::size_t origTheme = app.settings.theme;
  app.dispatchCommand(AppCommand::CycleTheme, sc, guard, lw, lh, w, h);
  assert(app.settings.mangaRtl == false);
  assert(app.settings.theme == origTheme);

  // Now under LTR: NextPage advances forward
  app.dispatchCommand(AppCommand::NextPage, sc, guard, lw, lh, w, h);
  assert(app.current->comic().currentPageIndex() == 1);

  // Zoom into FitWidth enables alignTopPending and overflow check when tall image loaded
  app.lastVisualImgW = 600;
  app.lastVisualImgH = 1200;
  app.dispatchCommand(AppCommand::FontIncrease, sc, guard, lw, lh, w, h);
  assert(app.zoomMode == cbz::ZoomMode::FitWidth);
  assert(app.isVisualZoomed(lw, lh));
  app.consumeAlignTop();
  assert(app.panY < -1e10f);

  // Opening Page Picker and pressing Back closes Picker without leaving book
  app.dispatchCommand(AppCommand::Confirm, sc, guard, lw, lh, w, h);
  assert(app.tocOpen);
  app.dispatchCommand(AppCommand::Back, sc, guard, lw, lh, w, h);
  assert(!app.tocOpen && !app.leaveBook && app.current.has_value());

  // PC config (Pconfig): T still cycles theme even inside CBZ
  Pconfig pc;
  app.dispatchCommand(AppCommand::CycleTheme, pc, guard, lw, lh, w, h);
  assert(app.settings.theme == (origTheme + 1) % theme::THEMES.size());

  std::error_code ec;
  fs::remove_all(progDir, ec);
}

} // namespace

int main() {
  std::cout << "Running NX-RaptureReader C++17 tests...\n";
  runTest("epub::loads_sample_epub", testEpubLoadsSample);
  runTest("epub::reads_metadata_only", testEpubMetadataOnly);
  runTest("epub::decodes_entities", testEpubDecodesEntities);
  runTest("epub::splits_blocks", testEpubSplitsBlocks);
  runTest("epub::messy_paths_and_inline_styles",
          testEpubMessyPathsAndInlineStyles);
  runTest("epub::toc_ncx_nav_and_cover", testEpubTocNcxNavAndCover);
  runTest("epub::namespaced_ncx_comments_and_br",
          testEpubNamespacedNcxCommentsAndBr);
  runTest("zip::streaming_reader_and_listing", testZipStreamingReaderAndListing);
  runTest("zip::zip64_archive_support", testZip64ArchiveSupport);
  runTest("cbz::natural_sort_and_sample_load", testCbzNaturalSortAndSampleLoad);
  runTest("cbz::webp_decode_and_zoom_pan", testCbzWebpDecodeAndZoomPan);
  runTest("cbz::has_overflow_check", testCbzHasOverflowCheck);
  runTest("cbz::library_scan_and_comicinfo", testCbzLibraryScanAndComicInfo);
  runTest("cbz::fit_rect_and_headless_render", testCbzFitRectAndHeadlessRender);
  runTest("pdf::loads_sample_and_renders", testPdfLoadsSampleAndRenders);
  runTest("progress::roundtrip", testProgressRoundtrip);
  runTest("progress::reads_legacy_two_field_files", testProgressLegacy);
  runTest("progress::ignores_corrupt_file", testProgressCorrupt);
  runTest("progress::distinct_paths_same_basename",
          testProgressDistinctPathsSameBasename);
  runTest("settings::roundtrip", testSettingsRoundtrip);
  runTest("settings::defaults_and_clamping", testSettingsClamping);
  runTest("settings::font_limits", testSettingsFontLimits);
  runTest("library::scan", testLibraryScan);
  runTest("library::scans_all_three_formats", testLibraryScansAllThreeFormats);
  runTest("text::kerning_and_styled_metrics", testTextKerningAndStyledMetrics);
  runTest("text::glyph_cache_bounded_and_invalidate",
          testTextGlyphCacheBoundedAndInvalidate);
  runTest("reader::bookmark_across_font_sizes", testReaderBookmarkAcrossFontSizes);
  runTest("reader::bookmark_across_rotation", testReaderBookmarkAcrossRotation);
  runTest("reader::restore_across_layouts", testReaderRestoreAcrossLayouts);
  runTest("reader::chapters_and_progress", testReaderChapterNavigationAndProgress);
  runTest("reader::multipage_chapter_heading_rule",
          testReaderMultiPageChapterHeadingRule);
  runTest("reader::lazy_per_chapter_pagination",
          testReaderLazyPerChapterPagination);
  runTest("reader::line_spacing_and_margins_preserve_bookmark",
          testReaderLineSpacingAndMarginsPreserveBookmark);
  runTest("reader::goto_chapter_and_toc_metadata",
          testReaderGotoChapterAndTocMetadata);
  runTest("touch::coordinates_and_zones", testTouchCoordinatesAndZones);
  runTest("touch::toc_zones_and_scroll_clamp", testTouchTocZones);
  runTest("sleep_guard::lifecycle_and_activity_filter", testSleepGuardLifecycle);
  runTest("app::controller_routing_and_switch_rtl",
          testAppControllerRoutingAndSwitchRtl);
  runTest("ui::oled_theme_and_portrait_rotation", testOledThemeAndRotationRender);
  runTest("ui::brightness_and_hidden_hud_render",
          testBrightnessAndHiddenHudRender);
  runTest("ui::toc_headless_render", testTocHeadlessRender);
  runTest("ui::covers_and_pdf_headless_render", testUiCoversAndPdfHeadlessRender);
  std::cout << gPassed << " tests passed.\n";
  return 0;
}
