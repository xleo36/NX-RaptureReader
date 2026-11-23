#include "library.hpp"
#include "cbz.hpp"
#include "epub.hpp"
#include "pdf.hpp"
#include "progress.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace fs = std::filesystem;

namespace library {
namespace {

std::string toLowerAscii(std::string s) {
  for (char &c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

bool isEpub(const fs::path &p) {
  if (!p.has_extension()) {
    return false;
  }
  return toLowerAscii(p.extension().string()) == ".epub";
}

bool isCbz(const fs::path &p) {
  if (!p.has_extension()) {
    return false;
  }
  return toLowerAscii(p.extension().string()) == ".cbz";
}

bool isPdf(const fs::path &p) {
  if (!p.has_extension()) {
    return false;
  }
  return toLowerAscii(p.extension().string()) == ".pdf";
}

bool isSupportedBook(const fs::path &p) {
  return isEpub(p) || isCbz(p) || isPdf(p);
}

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

bool isIgnoredEntryName(const std::string &name) {
  return name.empty() || name[0] == '.' || name == "__MACOSX";
}

constexpr int MAX_SCAN_DEPTH = 4;

void collectBooksFromDir(const fs::path &dir, int depth,
                         std::vector<std::string> &paths) {
  std::error_code dirEc;
  fs::directory_iterator it(dir, dirEc);
  if (dirEc) {
    return;
  }
  for (fs::directory_iterator end; it != end; it.increment(dirEc)) {
    if (dirEc) {
      break;
    }
    const fs::path &entryPath = it->path();
    const std::string name = entryPath.filename().string();
    if (isIgnoredEntryName(name)) {
      continue;
    }
    std::error_code itemEc;
    if (fs::is_directory(entryPath, itemEc) && !itemEc &&
        depth < MAX_SCAN_DEPTH) {
      collectBooksFromDir(entryPath, depth + 1, paths);
    } else if (fs::is_regular_file(entryPath, itemEc) && !itemEc &&
               isSupportedBook(entryPath)) {
      paths.push_back(entryPath.lexically_normal().string());
    }
  }
}

} // namespace

bool isComicPath(const std::string &path) { return isCbz(fs::path(path)); }
bool isPdfPath(const std::string &path) { return isPdf(fs::path(path)); }

std::vector<std::string> defaultSources() {
#ifdef __SWITCH__
  return {"sdmc:/switch/NX-RaptureReader/books", "sdmc:/switch/NX-RaptureReader",
          "romfs:/books"};
#else
  return {"books", (fs::path(progress::defaultDir()) / "books").string()};
#endif
}

std::vector<Entry> scan(const std::vector<std::string> &sources,
                        const std::string &progressDir) {
  std::vector<std::string> paths;

  for (const auto &src : sources) {
    fs::path sp(src);
    std::error_code srcEc;
    if (fs::is_directory(sp, srcEc) && !srcEc) {
      collectBooksFromDir(sp, 0, paths);
    } else if (fs::is_regular_file(sp, srcEc) && !srcEc &&
               isSupportedBook(sp)) {
      paths.push_back(sp.lexically_normal().string());
    }
  }

  std::sort(paths.begin(), paths.end());
  paths.erase(std::unique(paths.begin(), paths.end()), paths.end());

  std::vector<Entry> entries;
  entries.reserve(paths.size());

  for (auto &path : paths) {
    Entry e;
    e.path = path;
    std::string err;
    if (isComicPath(path)) {
      e.format = Format::Cbz;
      if (auto meta = cbz::metadata(path, err)) {
        e.title = meta->title;
        e.author = meta->author.empty()
                       ? ("CBZ (" + std::to_string(meta->pageCount) + " pages)")
                       : meta->author;
      } else {
        e.author = "CBZ";
      }
    } else if (isPdfPath(path)) {
      e.format = Format::Pdf;
      if (auto meta = pdf::metadata(path, err)) {
        e.title = meta->title;
        e.author = meta->author.empty()
                       ? ("PDF (" + std::to_string(meta->pageCount) + " pages)")
                       : meta->author;
      } else {
        e.author = "PDF";
      }
    } else {
      e.format = Format::Epub;
      if (auto meta = epub::metadata(path, err)) {
        e.title = meta->title;
        e.author = meta->author;
      }
    }
    if (trim(e.title).empty()) {
      std::string stem = fs::path(path).stem().string();
      std::replace(stem.begin(), stem.end(), '_', ' ');
      e.title = stem.empty() ? "Unknown" : stem;
    }
    entries.push_back(std::move(e));
  }

  std::sort(entries.begin(), entries.end(),
            [](const Entry &a, const Entry &b) {
              return toLowerAscii(a.title) < toLowerAscii(b.title);
            });

  refreshProgress(entries, progressDir);
  return entries;
}

void refreshProgress(std::vector<Entry> &entries,
                     const std::string &progressDir) {
  for (auto &entry : entries) {
    auto p = progress::load(progressDir, entry.path);
    entry.permille = p ? std::optional<uint32_t>(p->permille) : std::nullopt;
  }
}

} // namespace library
