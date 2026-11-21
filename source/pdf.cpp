#include "pdf.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>

#if defined(HAVE_MUPDF)
extern "C" {
#include <mupdf/fitz.h>
extern const fz_document_handler pdf_document_handler;
}
#elif defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#endif

namespace fs = std::filesystem;

namespace pdf {
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

std::string cleanedStem(const std::string &path) {
  std::string stem = fs::path(path).stem().string();
  std::replace(stem.begin(), stem.end(), '_', ' ');
  stem = trim(stem);
  return stem.empty() ? "Unknown PDF" : stem;
}

} // namespace

#if defined(HAVE_MUPDF)

struct PdfReader::Impl {
  fz_context *ctx = nullptr;
  fz_document *doc = nullptr;

  ~Impl() {
    if (ctx) {
      if (doc) {
        fz_drop_document(ctx, doc);
      }
      fz_drop_context(ctx);
    }
  }
};

namespace {

bool openImpl(const std::string &path, std::shared_ptr<PdfReader::Impl> &out,
              Metadata &meta, std::string &err) {
  auto impl = std::make_shared<PdfReader::Impl>();
  impl->ctx = fz_new_context(nullptr, nullptr, 32 * 1024 * 1024);
  if (!impl->ctx) {
    err = "MuPDF context creation failed";
    return false;
  }

  fz_document *volatile opened = nullptr;
  int volatile pages = 0;
  volatile int failed = 0;
  const char *volatile caughtMsg = nullptr;
  char titleBuf[512] = {};
  char authorBuf[512] = {};

  fz_try(impl->ctx) {
    fz_register_document_handler(impl->ctx, &pdf_document_handler);
    opened = fz_open_document(impl->ctx, path.c_str());
    if (fz_needs_password(impl->ctx, opened)) {
      fz_throw(impl->ctx, FZ_ERROR_GENERIC, "Password-protected PDF");
    }
    pages = fz_count_pages(impl->ctx, opened);
    if (pages <= 0) {
      fz_throw(impl->ctx, FZ_ERROR_GENERIC, "PDF has no pages");
    }
    fz_lookup_metadata(impl->ctx, opened, FZ_META_INFO_TITLE, titleBuf,
                       sizeof(titleBuf));
    fz_lookup_metadata(impl->ctx, opened, FZ_META_INFO_AUTHOR, authorBuf,
                       sizeof(authorBuf));
  }
  fz_catch(impl->ctx) {
    failed = 1;
    caughtMsg = fz_caught_message(impl->ctx);
    if (opened) {
      fz_drop_document(impl->ctx, opened);
      opened = nullptr;
    }
  }

  if (failed || !opened) {
    err = (caughtMsg && caughtMsg[0] != '\0') ? caughtMsg : "Cannot open PDF";
    return false;
  }

  meta.title = trim(titleBuf);
  meta.author = trim(authorBuf);
  impl->doc = opened;
  meta.pageCount = static_cast<std::size_t>(pages);
  if (meta.title.empty()) {
    meta.title = cleanedStem(path);
  }
  out = std::move(impl);
  return true;
}

} // namespace

bool PdfReader::renderPage(std::size_t idx, int targetW, int targetH,
                           cbz::DecodedImage &out, std::string &err) const {
  out = cbz::DecodedImage{};
  if (!impl_ || !impl_->ctx || !impl_->doc || idx >= meta_.pageCount) {
    err = "PDF page out of range";
    return false;
  }
  if (targetW <= 0 || targetH <= 0) {
    err = "Invalid target dimensions";
    return false;
  }

  fz_page *volatile page = nullptr;
  fz_pixmap *volatile pix = nullptr;
  fz_device *volatile dev = nullptr;
  volatile int failed = 0;
  const char *volatile caughtMsg = nullptr;

  fz_try(impl_->ctx) {
    page = fz_load_page(impl_->ctx, impl_->doc, static_cast<int>(idx));
    fz_rect bounds = fz_bound_page(impl_->ctx, page);
    float pageW = std::max(1.0f, bounds.x1 - bounds.x0);
    float pageH = std::max(1.0f, bounds.y1 - bounds.y0);
    float scale = std::min(static_cast<float>(targetW) / pageW,
                           static_cast<float>(targetH) / pageH);
    scale = std::clamp(scale, 0.2f, 4.0f);

    fz_matrix ctm = fz_scale(scale, scale);
    fz_irect bbox = fz_round_rect(fz_transform_rect(bounds, ctm));
    pix = fz_new_pixmap_with_bbox(impl_->ctx, fz_device_rgb(impl_->ctx), bbox,
                                  nullptr, 1);
    fz_clear_pixmap_with_value(impl_->ctx, pix, 255);
    dev = fz_new_draw_device(impl_->ctx, fz_identity, pix);
    fz_run_page(impl_->ctx, page, dev, ctm, nullptr);
    fz_close_device(impl_->ctx, dev);
  }
  fz_always(impl_->ctx) {
    if (dev) {
      fz_drop_device(impl_->ctx, dev);
      dev = nullptr;
    }
    if (page) {
      fz_drop_page(impl_->ctx, page);
      page = nullptr;
    }
  }
  fz_catch(impl_->ctx) {
    failed = 1;
    caughtMsg = fz_caught_message(impl_->ctx);
    if (pix) {
      fz_drop_pixmap(impl_->ctx, pix);
      pix = nullptr;
    }
  }

  if (failed || !pix) {
    if (pix) {
      fz_drop_pixmap(impl_->ctx, pix);
    }
    err = (caughtMsg && caughtMsg[0] != '\0') ? caughtMsg : "PDF render failed";
    return false;
  }

  int width = pix->w;
  int height = pix->h;
  if (width > 0 && height > 0) {
    out.width = width;
    out.height = height;
    out.rgba.resize(static_cast<std::size_t>(width) * height * 4);
    const uint8_t *src = fz_pixmap_samples(impl_->ctx, pix);
    int stride = fz_pixmap_stride(impl_->ctx, pix);
    for (int y = 0; y < height; ++y) {
      std::memcpy(out.rgba.data() + static_cast<std::size_t>(y) * width * 4,
                  src + static_cast<std::size_t>(y) * stride,
                  static_cast<std::size_t>(width) * 4);
    }
  }
  fz_drop_pixmap(impl_->ctx, pix);

  if (out.rgba.empty()) {
    out = cbz::DecodedImage{};
    err = "PDF rendered empty pixmap";
    return false;
  }
  return true;
}

#elif defined(__APPLE__)

struct PdfReader::Impl {
  CGPDFDocumentRef doc = nullptr;

  ~Impl() {
    if (doc) {
      CGPDFDocumentRelease(doc);
    }
  }
};

namespace {

std::string cfStringToUtf8(CFStringRef str) {
  if (!str) {
    return "";
  }
  CFIndex len = CFStringGetLength(str);
  CFIndex maxBytes =
      CFStringGetMaximumSizeForEncoding(len, kCFStringEncodingUTF8) + 1;
  std::string out(static_cast<std::size_t>(maxBytes), '\0');
  if (CFStringGetCString(str, out.data(), maxBytes, kCFStringEncodingUTF8)) {
    out.resize(std::strlen(out.c_str()));
    return trim(out);
  }
  return "";
}

std::string pdfInfoString(CGPDFDictionaryRef info, const char *key) {
  if (!info) {
    return "";
  }
  CGPDFStringRef pdfStr = nullptr;
  if (!CGPDFDictionaryGetString(info, key, &pdfStr) || !pdfStr) {
    return "";
  }
  CFStringRef cfStr = CGPDFStringCopyTextString(pdfStr);
  if (!cfStr) {
    return "";
  }
  std::string res = cfStringToUtf8(cfStr);
  CFRelease(cfStr);
  return res;
}

bool openImpl(const std::string &path, std::shared_ptr<PdfReader::Impl> &out,
              Metadata &meta, std::string &err) {
  std::error_code ec;
  fs::path absPath = fs::absolute(fs::path(path), ec);
  std::string resolved = ec ? path : absPath.string();

  CFURLRef url = CFURLCreateFromFileSystemRepresentation(
      kCFAllocatorDefault, reinterpret_cast<const UInt8 *>(resolved.c_str()),
      static_cast<CFIndex>(resolved.size()), false);
  if (!url) {
    err = "Invalid PDF path";
    return false;
  }
  CGPDFDocumentRef doc = CGPDFDocumentCreateWithURL(url);
  CFRelease(url);
  if (!doc) {
    err = "Cannot open PDF";
    return false;
  }
  if (CGPDFDocumentIsEncrypted(doc) && !CGPDFDocumentIsUnlocked(doc)) {
    if (!CGPDFDocumentUnlockWithPassword(doc, "")) {
      CGPDFDocumentRelease(doc);
      err = "PDF is password protected";
      return false;
    }
  }

  std::size_t pages = CGPDFDocumentGetNumberOfPages(doc);
  if (pages == 0) {
    CGPDFDocumentRelease(doc);
    err = "PDF has no pages";
    return false;
  }

  CGPDFDictionaryRef info = CGPDFDocumentGetInfo(doc);
  meta.title = pdfInfoString(info, "Title");
  meta.author = pdfInfoString(info, "Author");
  meta.pageCount = pages;
  if (meta.title.empty()) {
    meta.title = cleanedStem(path);
  }

  auto impl = std::make_shared<PdfReader::Impl>();
  impl->doc = doc;
  out = std::move(impl);
  return true;
}

} // namespace

bool PdfReader::renderPage(std::size_t idx, int targetW, int targetH,
                           cbz::DecodedImage &out, std::string &err) const {
  out = cbz::DecodedImage{};
  if (!impl_ || !impl_->doc || idx >= meta_.pageCount) {
    err = "PDF page out of range";
    return false;
  }
  if (targetW <= 0 || targetH <= 0) {
    err = "Invalid target dimensions";
    return false;
  }

  // CoreGraphics PDF pages are 1-indexed
  CGPDFPageRef page = CGPDFDocumentGetPage(impl_->doc, idx + 1);
  if (!page) {
    err = "Missing PDF page";
    return false;
  }

  CGRect box = CGPDFPageGetBoxRect(page, kCGPDFCropBox);
  if (CGRectIsEmpty(box)) {
    box = CGPDFPageGetBoxRect(page, kCGPDFMediaBox);
  }
  int rot = CGPDFPageGetRotationAngle(page);
  double pageW =
      (rot % 180 != 0) ? static_cast<double>(box.size.height)
                       : static_cast<double>(box.size.width);
  double pageH =
      (rot % 180 != 0) ? static_cast<double>(box.size.width)
                       : static_cast<double>(box.size.height);
  if (pageW <= 0.0 || pageH <= 0.0) {
    err = "Empty PDF page bounds";
    return false;
  }

  double scale = std::min(static_cast<double>(targetW) / pageW,
                          static_cast<double>(targetH) / pageH);
  scale = std::clamp(scale, 0.2, 4.0);
  int outW = std::clamp(static_cast<int>(std::lround(pageW * scale)), 1, 4096);
  int outH = std::clamp(static_cast<int>(std::lround(pageH * scale)), 1, 4096);

  std::vector<uint8_t> rgba(static_cast<std::size_t>(outW) * outH * 4, 255);
  CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
  if (!cs) {
    err = "CGColorSpaceCreateDeviceRGB failed";
    return false;
  }

  CGBitmapInfo bmpInfo = static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedLast) |
                         static_cast<CGBitmapInfo>(kCGBitmapByteOrder32Big);
  CGContextRef ctx = CGBitmapContextCreate(rgba.data(), outW, outH, 8, outW * 4,
                                           cs, bmpInfo);
  CGColorSpaceRelease(cs);
  if (!ctx) {
    err = "CGBitmapContextCreate failed";
    return false;
  }

  CGContextSetRGBFillColor(ctx, 1.0, 1.0, 1.0, 1.0);
  CGContextFillRect(ctx, CGRectMake(0, 0, outW, outH));
  CGContextSaveGState(ctx);
  CGAffineTransform xform = CGPDFPageGetDrawingTransform(
      page, kCGPDFCropBox, CGRectMake(0, 0, outW, outH), 0, true);
  CGContextConcatCTM(ctx, xform);
  CGContextDrawPDFPage(ctx, page);
  CGContextRestoreGState(ctx);
  CGContextRelease(ctx);

  out.width = outW;
  out.height = outH;
  out.rgba = std::move(rgba);
  return true;
}

#else

struct PdfReader::Impl {};

namespace {
bool openImpl(const std::string &, std::shared_ptr<PdfReader::Impl> &,
              Metadata &, std::string &err) {
  err = "PDF support not enabled on this platform";
  return false;
}
} // namespace

bool PdfReader::renderPage(std::size_t, int, int, cbz::DecodedImage &,
                           std::string &err) const {
  err = "PDF support not enabled on this platform";
  return false;
}

#endif

std::optional<Metadata> metadata(const std::string &path, std::string &err) {
  std::shared_ptr<PdfReader::Impl> impl;
  Metadata meta;
  if (!openImpl(path, impl, meta, err)) {
    return std::nullopt;
  }
  return meta;
}

bool renderCover(const std::string &path, int maxW, int maxH,
                 cbz::DecodedImage &out, std::string &err) {
  auto reader = PdfReader::open(path, err);
  if (!reader) {
    return false;
  }
  cbz::DecodedImage raw;
  if (!reader->renderPage(0, std::max(maxW * 2, 160), std::max(maxH * 2, 220),
                          raw, err)) {
    return false;
  }
  out = cbz::downsampleRgba(raw, maxW, maxH);
  return out.width > 0 && out.height > 0 && !out.rgba.empty();
}

std::optional<PdfReader> PdfReader::open(const std::string &path,
                                         std::string &err) {
  PdfReader r;
  if (!openImpl(path, r.impl_, r.meta_, err)) {
    return std::nullopt;
  }
  return r;
}

void PdfReader::restore(std::pair<std::size_t, std::size_t> anchor) {
  if (anchor.first < meta_.pageCount) {
    currentPage_ = anchor.first;
  }
}

std::optional<std::pair<std::size_t, std::size_t>> PdfReader::anchor() const {
  if (meta_.pageCount == 0) {
    return std::nullopt;
  }
  return std::pair<std::size_t, std::size_t>{currentPage_, 0};
}

uint32_t PdfReader::permille() const {
  if (meta_.pageCount <= 1) {
    return 1000;
  }
  return static_cast<uint32_t>(std::min<std::size_t>(
      1000, (currentPage_ * 1000) / (meta_.pageCount - 1)));
}

void PdfReader::nextPage() {
  if (currentPage_ + 1 < meta_.pageCount) {
    ++currentPage_;
  }
}

void PdfReader::prevPage() {
  if (currentPage_ > 0) {
    --currentPage_;
  }
}

void PdfReader::jumpBy(int delta) {
  if (meta_.pageCount == 0) {
    return;
  }
  int64_t maxIdx = static_cast<int64_t>(meta_.pageCount) - 1;
  int64_t next = std::clamp<int64_t>(
      static_cast<int64_t>(currentPage_) + delta, 0, maxIdx);
  currentPage_ = static_cast<std::size_t>(next);
}

void PdfReader::gotoPage(std::size_t idx) {
  if (idx < meta_.pageCount) {
    currentPage_ = idx;
  }
}

std::string PdfReader::pageDisplayTitle(std::size_t idx) const {
  return "Page " + std::to_string(idx + 1);
}

uint32_t PdfReader::pagePercent(std::size_t idx) const {
  if (meta_.pageCount <= 1 || idx >= meta_.pageCount) {
    return meta_.pageCount == 0 ? 0 : 100;
  }
  return static_cast<uint32_t>(
      std::min<std::size_t>(100, (idx * 100) / (meta_.pageCount - 1)));
}

} // namespace pdf
