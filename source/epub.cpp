#include "epub.hpp"
#include "zip_reader.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <unordered_map>

namespace epub {
namespace {

int hexVal(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return 10 + (c - 'a');
  }
  if (c >= 'A' && c <= 'F') {
    return 10 + (c - 'A');
  }
  return -1;
}

bool readZipEntry(const zip_util::ZipReader &zip, const std::string &name,
                  std::string &out, std::string &err) {
  if (zip.hasEntry(name)) {
    return zip.readEntry(name, out, err);
  }
  std::string norm = resolveZipPath("", name);
  if (zip.hasEntry(norm)) {
    return zip.readEntry(norm, out, err);
  }
  for (const auto &entry : zip.entries()) {
    if (resolveZipPath("", entry) == norm) {
      return zip.readEntry(entry, out, err);
    }
  }
  err = "Missing '" + name + "' in EPUB";
  return false;
}

bool readZipBytes(const zip_util::ZipReader &zip, const std::string &name,
                  std::vector<uint8_t> &out, std::string &err) {
  if (zip.hasEntry(name)) {
    return zip.readEntryBytes(name, out, err);
  }
  std::string norm = resolveZipPath("", name);
  if (zip.hasEntry(norm)) {
    return zip.readEntryBytes(norm, out, err);
  }
  for (const auto &entry : zip.entries()) {
    if (resolveZipPath("", entry) == norm) {
      return zip.readEntryBytes(entry, out, err);
    }
  }
  err = "Missing '" + name + "' in EPUB";
  return false;
}

std::string parentDir(const std::string &path) {
  auto slash = path.rfind('/');
  return (slash == std::string::npos) ? std::string{} : path.substr(0, slash);
}

std::string toLowerAscii(std::string s) {
  for (char &c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
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

void appendUtf8(std::string &out, uint32_t cp) {
  if (cp <= 0x7Fu) {
    out.push_back(static_cast<char>(cp));
  } else if (cp <= 0x7FFu) {
    out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
    out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
  } else if (cp <= 0xFFFFu) {
    out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
    out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
    out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
  } else if (cp <= 0x10FFFFu) {
    out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
    out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
    out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
    out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
  }
}

std::string stripXmlPrefix(const std::string &tag) {
  auto colon = tag.find(':');
  return (colon == std::string::npos) ? tag : tag.substr(colon + 1);
}

struct TagInfo {
  std::string name;
  std::string attrs;
};

std::vector<TagInfo> extractTags(const std::string &s) {
  std::vector<TagInfo> result;
  std::size_t pos = 0;
  while (pos < s.size()) {
    std::size_t lt = s.find('<', pos);
    if (lt == std::string::npos) {
      break;
    }
    if (s.compare(lt, 4, "<!--") == 0) {
      std::size_t endComment = s.find("-->", lt + 4);
      if (endComment == std::string::npos) {
        break;
      }
      pos = endComment + 3;
      continue;
    }
    std::size_t gt = s.find('>', lt);
    if (gt == std::string::npos) {
      break;
    }
    std::string inner = s.substr(lt + 1, gt - lt - 1);
    pos = gt + 1;
    if (inner.empty() || inner[0] == '/' || inner[0] == '?' ||
        inner[0] == '!') {
      continue;
    }
    if (!inner.empty() && inner.back() == '/') {
      inner.pop_back();
    }
    std::size_t ws = std::string::npos;
    for (std::size_t i = 0; i < inner.size(); ++i) {
      if (std::isspace(static_cast<unsigned char>(inner[i]))) {
        ws = i;
        break;
      }
    }
    TagInfo ti;
    if (ws == std::string::npos) {
      ti.name = stripXmlPrefix(toLowerAscii(inner));
    } else {
      ti.name = stripXmlPrefix(toLowerAscii(inner.substr(0, ws)));
      ti.attrs = inner.substr(ws + 1);
    }
    result.push_back(std::move(ti));
  }
  return result;
}

std::optional<std::string> findAttr(const std::string &attrs,
                                    const std::string &key) {
  std::size_t pos = 0;
  while (pos < attrs.size()) {
    pos = attrs.find(key, pos);
    if (pos == std::string::npos) {
      return std::nullopt;
    }
    bool leftBoundary =
        (pos == 0 ||
         std::isspace(static_cast<unsigned char>(attrs[pos - 1])));
    std::size_t after = pos + key.size();
    while (after < attrs.size() &&
           std::isspace(static_cast<unsigned char>(attrs[after]))) {
      ++after;
    }
    if (!leftBoundary || after >= attrs.size() || attrs[after] != '=') {
      pos += key.size();
      continue;
    }
    ++after;
    while (after < attrs.size() &&
           std::isspace(static_cast<unsigned char>(attrs[after]))) {
      ++after;
    }
    if (after >= attrs.size()) {
      return std::nullopt;
    }
    char quote = attrs[after];
    if (quote != '"' && quote != '\'') {
      pos = after;
      continue;
    }
    std::size_t close = attrs.find(quote, after + 1);
    if (close == std::string::npos) {
      return std::nullopt;
    }
    return decodeEntities(attrs.substr(after + 1, close - (after + 1)));
  }
  return std::nullopt;
}

bool hasToken(const std::string &list, const std::string &token) {
  std::size_t pos = 0;
  while (pos < list.size()) {
    while (pos < list.size() &&
           std::isspace(static_cast<unsigned char>(list[pos]))) {
      ++pos;
    }
    std::size_t end = pos;
    while (end < list.size() &&
           !std::isspace(static_cast<unsigned char>(list[end]))) {
      ++end;
    }
    if (end > pos &&
        toLowerAscii(list.substr(pos, end - pos)) == toLowerAscii(token)) {
      return true;
    }
    pos = end;
  }
  return false;
}

std::string stripTagsAndCollapse(const std::string &frag) {
  std::string raw;
  raw.reserve(frag.size());
  bool inSpace = false;
  std::size_t i = 0;
  while (i < frag.size()) {
    if (frag.compare(i, 4, "<!--") == 0) {
      std::size_t endC = frag.find("-->", i + 4);
      i = (endC == std::string::npos) ? frag.size() : (endC + 3);
      continue;
    }
    if (frag[i] == '<') {
      std::size_t gt = frag.find('>', i + 1);
      i = (gt == std::string::npos) ? frag.size() : (gt + 1);
      continue;
    }
    if (std::isspace(static_cast<unsigned char>(frag[i]))) {
      if (!inSpace && !raw.empty()) {
        raw.push_back(' ');
      }
      inSpace = true;
    } else {
      raw.push_back(frag[i]);
      inSpace = false;
    }
    ++i;
  }
  return trim(decodeEntities(raw));
}

std::optional<std::string> rawElementInner(const std::string &s,
                                           const std::string &localName) {
  std::string target = toLowerAscii(localName);
  std::size_t pos = 0;
  while (pos < s.size()) {
    std::size_t lt = s.find('<', pos);
    if (lt == std::string::npos) {
      break;
    }
    if (s.compare(lt, 4, "<!--") == 0) {
      std::size_t endC = s.find("-->", lt + 4);
      pos = (endC == std::string::npos) ? s.size() : (endC + 3);
      continue;
    }
    std::size_t gt = s.find('>', lt + 1);
    if (gt == std::string::npos) {
      break;
    }
    pos = gt + 1;
    std::string inner = s.substr(lt + 1, gt - lt - 1);
    if (inner.empty() || inner[0] == '/' || inner[0] == '?' ||
        inner[0] == '!' || inner.back() == '/') {
      continue;
    }
    std::size_t ws = 0;
    while (ws < inner.size() &&
           !std::isspace(static_cast<unsigned char>(inner[ws]))) {
      ++ws;
    }
    if (stripXmlPrefix(toLowerAscii(inner.substr(0, ws))) != target) {
      continue;
    }
    std::size_t search = pos;
    while (search < s.size()) {
      std::size_t clt = s.find('<', search);
      if (clt == std::string::npos) {
        return std::nullopt;
      }
      if (s.compare(clt, 4, "<!--") == 0) {
        std::size_t endC = s.find("-->", clt + 4);
        search = (endC == std::string::npos) ? s.size() : (endC + 3);
        continue;
      }
      std::size_t cgt = s.find('>', clt + 1);
      if (cgt == std::string::npos) {
        return std::nullopt;
      }
      std::string cinner = trim(s.substr(clt + 1, cgt - clt - 1));
      if (!cinner.empty() && cinner[0] == '/' &&
          stripXmlPrefix(toLowerAscii(trim(cinner.substr(1)))) == target) {
        return s.substr(pos, clt - pos);
      }
      search = cgt + 1;
    }
    return std::nullopt;
  }
  return std::nullopt;
}

std::optional<std::string> elementText(const std::string &s,
                                       const std::string &localName) {
  auto raw = rawElementInner(s, localName);
  if (!raw) {
    return std::nullopt;
  }
  return stripTagsAndCollapse(*raw);
}

bool isBlockTag(const std::string &name) {
  return name == "p" || name == "div" || name == "h1" || name == "h2" ||
         name == "h3" || name == "h4" || name == "h5" || name == "h6" ||
         name == "li" || name == "blockquote" || name == "tr";
}

bool isHeadingTag(const std::string &name) {
  return name == "h1" || name == "h2" || name == "h3" || name == "h4" ||
         name == "h5" || name == "h6";
}

bool isItalicTag(const std::string &name) {
  return name == "em" || name == "i" || name == "cite";
}

bool isBoldTag(const std::string &name) {
  return name == "strong" || name == "b";
}

void parseNcxToc(const std::string &ncx, const std::string &ncxDir,
                 std::unordered_map<std::string, std::string> &out) {
  std::string currentLabel;
  std::size_t pos = 0;
  while (pos < ncx.size()) {
    std::size_t lt = ncx.find('<', pos);
    if (lt == std::string::npos) {
      break;
    }
    if (ncx.compare(lt, 4, "<!--") == 0) {
      std::size_t endC = ncx.find("-->", lt + 4);
      pos = (endC == std::string::npos) ? ncx.size() : (endC + 3);
      continue;
    }
    std::size_t gt = ncx.find('>', lt + 1);
    if (gt == std::string::npos) {
      break;
    }
    std::string inner = ncx.substr(lt + 1, gt - lt - 1);
    pos = gt + 1;
    std::size_t ws = 0;
    while (ws < inner.size() &&
           !std::isspace(static_cast<unsigned char>(inner[ws]))) {
      ++ws;
    }
    std::string tag = stripXmlPrefix(toLowerAscii(inner.substr(0, ws)));
    if (tag == "navpoint") {
      currentLabel.clear();
    } else if (tag == "text") {
      std::size_t endText = std::string::npos;
      // Find matching </text> or </ncx:text> case-insensitively
      std::size_t search = pos;
      while (search < ncx.size()) {
        std::size_t clt = ncx.find('<', search);
        if (clt == std::string::npos) {
          break;
        }
        std::size_t cgt = ncx.find('>', clt + 1);
        if (cgt == std::string::npos) {
          break;
        }
        std::string cinner = trim(ncx.substr(clt + 1, cgt - clt - 1));
        if (!cinner.empty() && cinner[0] == '/' &&
            stripXmlPrefix(toLowerAscii(trim(cinner.substr(1)))) == "text") {
          endText = clt;
          pos = cgt + 1;
          break;
        }
        search = cgt + 1;
      }
      if (endText != std::string::npos) {
        currentLabel = stripTagsAndCollapse(
            ncx.substr(gt + 1, endText - (gt + 1)));
      }
    } else if (tag == "content") {
      std::string attrs = (ws < inner.size()) ? inner.substr(ws) : "";
      if (auto src = findAttr(attrs, "src")) {
        std::string target = resolveZipPath(ncxDir, *src);
        if (!target.empty() && !currentLabel.empty()) {
          out.emplace(std::move(target), currentLabel);
        }
      }
    }
  }
}

void parseNavXhtmlToc(const std::string &nav, const std::string &navDir,
                      std::unordered_map<std::string, std::string> &out) {
  // Locate `<nav ...>` containing TOC (`epub:type="toc"` or first `<nav>`)
  std::string section;
  std::string firstNav;
  std::size_t pos = 0;
  while (pos < nav.size()) {
    std::size_t lt = nav.find('<', pos);
    if (lt == std::string::npos) {
      break;
    }
    std::size_t gt = nav.find('>', lt + 1);
    if (gt == std::string::npos) {
      break;
    }
    std::string inner = nav.substr(lt + 1, gt - lt - 1);
    pos = gt + 1;
    std::size_t ws = 0;
    while (ws < inner.size() &&
           !std::isspace(static_cast<unsigned char>(inner[ws]))) {
      ++ws;
    }
    std::string tag = toLowerAscii(inner.substr(0, ws));
    if (tag == "nav") {
      std::size_t navEnd = pos;
      std::size_t depth = 1;
      while (navEnd < nav.size() && depth > 0) {
        std::size_t nlt = nav.find('<', navEnd);
        if (nlt == std::string::npos) {
          navEnd = nav.size();
          break;
        }
        std::size_t ngt = nav.find('>', nlt + 1);
        if (ngt == std::string::npos) {
          navEnd = nav.size();
          break;
        }
        std::string ninner = nav.substr(nlt + 1, ngt - nlt - 1);
        std::size_t nws = 0;
        while (nws < ninner.size() &&
               !std::isspace(static_cast<unsigned char>(ninner[nws]))) {
          ++nws;
        }
        std::string ntag = toLowerAscii(ninner.substr(0, nws));
        if (ntag == "nav") {
          ++depth;
        } else if (ntag == "/nav") {
          --depth;
          if (depth == 0) {
            navEnd = nlt;
            break;
          }
        }
        navEnd = ngt + 1;
      }
      std::string body = nav.substr(pos, navEnd - pos);
      std::string attrs = (ws < inner.size()) ? inner.substr(ws) : "";
      if (firstNav.empty()) {
        firstNav = body;
      }
      auto epubType = findAttr(attrs, "epub:type");
      auto roleAttr = findAttr(attrs, "role");
      if ((epubType && hasToken(*epubType, "toc")) ||
          (roleAttr && hasToken(*roleAttr, "doc-toc"))) {
        section = std::move(body);
        break;
      }
      pos = navEnd;
    }
  }
  if (section.empty()) {
    section = std::move(firstNav);
  }
  if (section.empty()) {
    return;
  }

  pos = 0;
  while (pos < section.size()) {
    std::size_t lt = section.find('<', pos);
    if (lt == std::string::npos) {
      break;
    }
    std::size_t gt = section.find('>', lt + 1);
    if (gt == std::string::npos) {
      break;
    }
    std::string inner = section.substr(lt + 1, gt - lt - 1);
    pos = gt + 1;
    std::size_t ws = 0;
    while (ws < inner.size() &&
           !std::isspace(static_cast<unsigned char>(inner[ws]))) {
      ++ws;
    }
    if (toLowerAscii(inner.substr(0, ws)) != "a") {
      continue;
    }
    std::string attrs = (ws < inner.size()) ? inner.substr(ws) : "";
    auto href = findAttr(attrs, "href");

    std::size_t endA = pos;
    while (endA < section.size()) {
      std::size_t alt = section.find('<', endA);
      if (alt == std::string::npos) {
        endA = section.size();
        break;
      }
      std::size_t agt = section.find('>', alt + 1);
      if (agt == std::string::npos) {
        endA = section.size();
        break;
      }
      if (toLowerAscii(trim(section.substr(alt + 1, agt - alt - 1))) == "/a") {
        std::string label =
            stripTagsAndCollapse(section.substr(pos, alt - pos));
        pos = agt + 1;
        if (href && !label.empty()) {
          std::string target = resolveZipPath(navDir, *href);
          if (!target.empty()) {
            out.emplace(std::move(target), std::move(label));
          }
        }
        break;
      }
      endA = agt + 1;
    }
  }
}

bool readPackage(const zip_util::ZipReader &zip, std::string &opfPath,
                 std::string &opf, std::string &err) {
  std::string container;
  if (!readZipEntry(zip, "META-INF/container.xml", container, err)) {
    return false;
  }
  for (const auto &t : extractTags(container)) {
    if (t.name == "rootfile") {
      auto fp = findAttr(t.attrs, "full-path");
      if (fp) {
        opfPath = resolveZipPath("", *fp);
        return readZipEntry(zip, opfPath, opf, err);
      }
    }
  }
  err = "container.xml has no rootfile";
  return false;
}

} // namespace

std::string urlDecode(const std::string &s) {
  std::string out;
  out.reserve(s.size());
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%' && i + 2 < s.size()) {
      int hi = hexVal(s[i + 1]);
      int lo = hexVal(s[i + 2]);
      if (hi >= 0 && lo >= 0) {
        out.push_back(static_cast<char>((hi << 4) | lo));
        i += 2;
        continue;
      }
    }
    out.push_back(s[i]);
  }
  return out;
}

std::string resolveZipPath(const std::string &baseDir,
                           const std::string &href) {
  std::string clean = decodeEntities(trim(href));
  auto frag = clean.find_first_of("#?");
  if (frag != std::string::npos) {
    clean.resize(frag);
  }
  clean = urlDecode(clean);
  std::replace(clean.begin(), clean.end(), '\\', '/');

  std::string combined;
  if (!clean.empty() && clean[0] == '/') {
    combined = clean;
  } else if (baseDir.empty()) {
    combined = clean;
  } else {
    combined = baseDir + "/" + clean;
  }

  std::vector<std::string> segments;
  std::size_t pos = 0;
  while (pos < combined.size()) {
    std::size_t slash = combined.find('/', pos);
    std::string seg = (slash == std::string::npos)
                          ? combined.substr(pos)
                          : combined.substr(pos, slash - pos);
    pos = (slash == std::string::npos) ? combined.size() : (slash + 1);
    if (seg.empty() || seg == ".") {
      continue;
    }
    if (seg == "..") {
      if (!segments.empty()) {
        segments.pop_back();
      }
    } else {
      segments.push_back(std::move(seg));
    }
  }

  std::string out;
  for (std::size_t i = 0; i < segments.size(); ++i) {
    if (i > 0) {
      out.push_back('/');
    }
    out += segments[i];
  }
  return out;
}

std::string decodeEntities(const std::string &s) {
  std::string out;
  out.reserve(s.size());
  std::size_t i = 0;
  while (i < s.size()) {
    std::size_t amp = s.find('&', i);
    if (amp == std::string::npos) {
      out.append(s, i, std::string::npos);
      break;
    }
    out.append(s, i, amp - i);
    std::size_t semi = s.find(';', amp);
    bool decoded = false;
    if (semi != std::string::npos && semi - amp <= 10) {
      std::string ent = s.substr(amp + 1, semi - amp - 1);
      if (ent == "amp") {
        out.push_back('&');
        decoded = true;
      } else if (ent == "lt") {
        out.push_back('<');
        decoded = true;
      } else if (ent == "gt") {
        out.push_back('>');
        decoded = true;
      } else if (ent == "quot") {
        out.push_back('"');
        decoded = true;
      } else if (ent == "apos") {
        out.push_back('\'');
        decoded = true;
      } else if (ent == "nbsp") {
        out.push_back(' ');
        decoded = true;
      } else if (ent.size() >= 2 && ent[0] == '#') {
        uint32_t cp = 0;
        bool valid = false;
        if (ent.size() >= 3 && (ent[1] == 'x' || ent[1] == 'X')) {
          valid = true;
          for (std::size_t k = 2; k < ent.size(); ++k) {
            if (!std::isxdigit(static_cast<unsigned char>(ent[k]))) {
              valid = false;
              break;
            }
          }
          if (valid) {
            cp = static_cast<uint32_t>(std::stoul(ent.substr(2), nullptr, 16));
          }
        } else {
          valid = true;
          for (std::size_t k = 1; k < ent.size(); ++k) {
            if (!std::isdigit(static_cast<unsigned char>(ent[k]))) {
              valid = false;
              break;
            }
          }
          if (valid) {
            cp = static_cast<uint32_t>(std::stoul(ent.substr(1), nullptr, 10));
          }
        }
        if (valid && cp > 0 && cp <= 0x10FFFFu) {
          appendUtf8(out, cp);
          decoded = true;
        }
      }
    }
    if (decoded) {
      i = semi + 1;
    } else {
      out.push_back('&');
      i = amp + 1;
    }
  }
  return out;
}

std::vector<Block> htmlToBlocks(const std::string &html) {
  std::vector<Block> blocks;
  std::string currentText;
  std::vector<StyleSpan> currentSpans;
  bool heading = false;
  unsigned int skipDepth = 0;
  unsigned int italicDepth = 0;
  unsigned int boldDepth = 0;
  bool pendingSpace = false;
  uint8_t pendingSpaceStyle = StyleRegular;

  auto currentStyle = [&]() -> uint8_t {
    uint8_t st = StyleRegular;
    if (italicDepth > 0) {
      st |= StyleItalic;
    }
    if (boldDepth > 0) {
      st |= StyleBold;
    }
    return st;
  };

  auto pushByte = [&](char c, uint8_t style) {
    std::size_t idx = currentText.size();
    currentText.push_back(c);
    if (style != StyleRegular) {
      if (!currentSpans.empty() && currentSpans.back().end == idx &&
          currentSpans.back().style == style) {
        currentSpans.back().end = idx + 1;
      } else {
        currentSpans.push_back(StyleSpan{idx, idx + 1, style});
      }
    }
  };

  auto appendChunk = [&](std::string_view raw, uint8_t style) {
    if (raw.empty()) {
      return;
    }
    std::string decoded = decodeEntities(std::string(raw));
    for (unsigned char ch : decoded) {
      if (std::isspace(ch)) {
        if (!currentText.empty() && !pendingSpace) {
          pendingSpace = true;
          pendingSpaceStyle = style;
        }
      } else {
        if (pendingSpace) {
          pushByte(' ', pendingSpaceStyle);
          pendingSpace = false;
        }
        pushByte(static_cast<char>(ch), style);
      }
    }
  };

  auto flush = [&]() {
    if (!currentText.empty()) {
      blocks.push_back(Block{
          heading ? BlockKind::Heading : BlockKind::Paragraph,
          std::move(currentText),
          std::move(currentSpans),
      });
      currentText.clear();
      currentSpans.clear();
    }
    pendingSpace = false;
  };

  std::size_t pos = 0;
  while (pos < html.size()) {
    std::size_t lt = html.find('<', pos);
    if (lt == std::string::npos) {
      if (skipDepth == 0) {
        appendChunk(std::string_view(html).substr(pos), currentStyle());
      }
      break;
    }
    if (skipDepth == 0 && lt > pos) {
      appendChunk(std::string_view(html).substr(pos, lt - pos), currentStyle());
    }

    if (html.compare(lt, 4, "<!--") == 0) {
      std::size_t endComment = html.find("-->", lt + 4);
      if (endComment == std::string::npos) {
        break;
      }
      pos = endComment + 3;
      continue;
    }

    std::size_t gt = html.find('>', lt);
    if (gt == std::string::npos) {
      break;
    }
    std::string tag = html.substr(lt + 1, gt - lt - 1);
    pos = gt + 1;

    bool closing = !tag.empty() && tag[0] == '/';
    bool selfClosing = !tag.empty() && tag.back() == '/';
    std::size_t nameStart = closing ? 1 : 0;
    std::size_t nameEnd = nameStart;
    while (nameEnd < tag.size() &&
           !std::isspace(static_cast<unsigned char>(tag[nameEnd])) &&
           tag[nameEnd] != '/') {
      ++nameEnd;
    }
    std::string name =
        stripXmlPrefix(toLowerAscii(tag.substr(nameStart, nameEnd - nameStart)));

    if (name == "head" || name == "script" || name == "style") {
      if (closing) {
        if (skipDepth > 0) {
          --skipDepth;
        }
      } else if (!selfClosing) {
        ++skipDepth;
      }
    } else if (skipDepth == 0) {
      if (name == "br") {
        if (!closing && !currentText.empty() && currentText.back() != '\n') {
          currentText.push_back('\n');
        }
        pendingSpace = false;
      } else if (isBlockTag(name)) {
        flush();
        if (isHeadingTag(name)) {
          heading = !closing;
        }
        // Reset inline styles at block boundaries so malformed unclosed tags
        // cannot bleed across paragraphs.
        italicDepth = 0;
        boldDepth = 0;
      } else if (isItalicTag(name)) {
        if (closing) {
          if (italicDepth > 0) {
            --italicDepth;
          }
        } else if (!selfClosing) {
          ++italicDepth;
        }
      } else if (isBoldTag(name)) {
        if (closing) {
          if (boldDepth > 0) {
            --boldDepth;
          }
        } else if (!selfClosing) {
          ++boldDepth;
        }
      }
    }
  }
  flush();
  return blocks;
}

std::optional<Metadata> metadata(const std::string &path, std::string &err) {
  zip_util::ZipReader zip;
  if (!zip.open(path, err)) {
    return std::nullopt;
  }
  std::string opfPath, opf;
  if (!readPackage(zip, opfPath, opf, err)) {
    return std::nullopt;
  }
  std::string metaSec = rawElementInner(opf, "metadata").value_or(opf);
  Metadata m;
  m.title = elementText(metaSec, "title").value_or("");
  m.author = elementText(metaSec, "creator").value_or("");
  return m;
}

std::optional<Book> load(const std::string &path, std::string &err) {
  zip_util::ZipReader zip;
  if (!zip.open(path, err)) {
    return std::nullopt;
  }
  std::string opfPath, opf;
  if (!readPackage(zip, opfPath, opf, err)) {
    return std::nullopt;
  }

  std::string opfDir = parentDir(opfPath);
  std::string metaSec = rawElementInner(opf, "metadata").value_or(opf);

  Book book;
  book.title = elementText(metaSec, "title").value_or("Untitled");
  book.author = elementText(metaSec, "creator").value_or("");

  struct ManifestItem {
    std::string href;
    std::string mediaType;
    std::string properties;
  };
  std::unordered_map<std::string, ManifestItem> manifest;
  std::vector<std::string> spine;
  std::string spineTocId;
  std::string navPath;
  std::string ncxPath;

  for (const auto &t : extractTags(opf)) {
    if (t.name == "item") {
      auto id = findAttr(t.attrs, "id");
      auto href = findAttr(t.attrs, "href");
      if (id && href) {
        ManifestItem mi;
        mi.href = *href;
        mi.mediaType = findAttr(t.attrs, "media-type").value_or("");
        mi.properties = findAttr(t.attrs, "properties").value_or("");
        if (hasToken(mi.properties, "nav")) {
          navPath = resolveZipPath(opfDir, mi.href);
        }
        if (toLowerAscii(mi.mediaType) == "application/x-dtbncx+xml") {
          ncxPath = resolveZipPath(opfDir, mi.href);
        }
        manifest[*id] = std::move(mi);
      }
    } else if (t.name == "spine") {
      if (auto tocAttr = findAttr(t.attrs, "toc")) {
        spineTocId = *tocAttr;
      }
    } else if (t.name == "itemref") {
      auto idref = findAttr(t.attrs, "idref");
      if (idref) {
        auto it = manifest.find(*idref);
        if (it != manifest.end()) {
          spine.push_back(it->second.href);
        }
      }
    }
  }

  if (ncxPath.empty() && !spineTocId.empty()) {
    auto it = manifest.find(spineTocId);
    if (it != manifest.end()) {
      ncxPath = resolveZipPath(opfDir, it->second.href);
    }
  }

  std::unordered_map<std::string, std::string> tocTitles;
  if (!navPath.empty()) {
    std::string navXml;
    std::string ignoreErr;
    if (readZipEntry(zip, navPath, navXml, ignoreErr)) {
      parseNavXhtmlToc(navXml, parentDir(navPath), tocTitles);
    }
  }
  if (!ncxPath.empty()) {
    std::string ncxXml;
    std::string ignoreErr;
    if (readZipEntry(zip, ncxPath, ncxXml, ignoreErr)) {
      parseNcxToc(ncxXml, parentDir(ncxPath), tocTitles);
    }
  }

  if (spine.empty()) {
    err = "EPUB spine is empty";
    return std::nullopt;
  }

  std::size_t totalBytes = 0;
  for (const auto &href : spine) {
    std::string entry = resolveZipPath(opfDir, href);
    std::string html;
    if (!readZipEntry(zip, entry, html, err)) {
      return std::nullopt;
    }
    auto docBlocks = htmlToBlocks(html);
    if (docBlocks.empty()) {
      continue;
    }

    Chapter ch;
    ch.firstBlockIndex = book.blocks.size();
    ch.blockCount = docBlocks.size();
    ch.byteOffset = totalBytes;
    auto tocIt = tocTitles.find(entry);
    if (tocIt != tocTitles.end() && !tocIt->second.empty()) {
      ch.title = tocIt->second;
    } else if (docBlocks[0].kind == BlockKind::Heading) {
      ch.title = docBlocks[0].text;
    }
    for (const auto &b : docBlocks) {
      ch.byteLength += b.text.size();
    }
    totalBytes += ch.byteLength;

    book.chapterStarts.push_back(book.blocks.size());
    book.chapters.push_back(std::move(ch));
    book.blocks.insert(book.blocks.end(),
                       std::make_move_iterator(docBlocks.begin()),
                       std::make_move_iterator(docBlocks.end()));
  }

  return book;
}

bool extractCoverImage(const std::string &path, std::vector<uint8_t> &out,
                       std::string &err) {
  zip_util::ZipReader zip;
  if (!zip.open(path, err)) {
    return false;
  }
  std::string opfPath, opf;
  if (!readPackage(zip, opfPath, opf, err)) {
    return false;
  }
  std::string opfDir = parentDir(opfPath);

  struct CoverCand {
    std::string id;
    std::string href;
    std::string mediaType;
    std::string properties;
  };
  std::vector<CoverCand> items;
  std::string metaCoverId;

  for (const auto &t : extractTags(opf)) {
    if (t.name == "meta") {
      auto name = findAttr(t.attrs, "name");
      auto content = findAttr(t.attrs, "content");
      if (name && content && toLowerAscii(*name) == "cover") {
        metaCoverId = *content;
      }
    } else if (t.name == "item") {
      auto id = findAttr(t.attrs, "id");
      auto href = findAttr(t.attrs, "href");
      if (id && href) {
        items.push_back(CoverCand{
            *id, *href, findAttr(t.attrs, "media-type").value_or(""),
            findAttr(t.attrs, "properties").value_or("")});
      }
    }
  }

  std::string coverHref;
  for (const auto &it : items) {
    if (hasToken(it.properties, "cover-image")) {
      coverHref = it.href;
      break;
    }
  }
  if (coverHref.empty() && !metaCoverId.empty()) {
    for (const auto &it : items) {
      if (it.id == metaCoverId) {
        coverHref = it.href;
        break;
      }
    }
  }
  if (coverHref.empty()) {
    for (const auto &it : items) {
      if (toLowerAscii(it.mediaType).rfind("image/", 0) == 0 &&
          (toLowerAscii(it.id).find("cover") != std::string::npos ||
           toLowerAscii(it.href).find("cover") != std::string::npos)) {
        coverHref = it.href;
        break;
      }
    }
  }

  if (coverHref.empty()) {
    err = "No cover image in EPUB";
    return false;
  }
  return readZipBytes(zip, resolveZipPath(opfDir, coverHref), out, err);
}

} // namespace epub
