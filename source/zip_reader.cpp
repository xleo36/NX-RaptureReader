#include "zip_reader.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <zlib.h>

namespace zip_util {
namespace {

uint16_t readU16(const uint8_t *p) {
  return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

uint32_t readU32(const uint8_t *p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

uint64_t readU64(const uint8_t *p) {
  return static_cast<uint64_t>(readU32(p)) |
         (static_cast<uint64_t>(readU32(p + 4)) << 32);
}

/// True when [offset, offset + length) lies inside [0, total). Written so that
/// hostile 64-bit values cannot wrap around.
bool rangeWithin(uint64_t offset, uint64_t length, uint64_t total) {
  return offset <= total && length <= total - offset;
}

// A deflate stream cannot expand by more than ~1032:1, so a header claiming
// more than that is lying and must not drive a large up-front allocation.
constexpr uint64_t MAX_DEFLATE_RATIO = 1032;

/// Inflates a raw deflate stream that must produce exactly `expectedLen`
/// bytes. Output is capped at `expectedLen` while inflating, so a zip bomb
/// whose real size exceeds its header is rejected instead of exhausting RAM.
template <typename Buffer>
bool inflateRaw(const uint8_t *src, uint64_t srcLen, uint64_t expectedLen,
                Buffer &out, std::string &err) {
  out.clear();
  if (srcLen == 0 && expectedLen == 0) {
    return true;
  }
  if (srcLen > std::numeric_limits<uInt>::max()) {
    err = "ZIP entry too large to inflate";
    return false;
  }

  z_stream zs{};
  if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) {
    err = "zlib inflateInit2 failed";
    return false;
  }
  zs.next_in = const_cast<Bytef *>(reinterpret_cast<const Bytef *>(src));
  zs.avail_in = static_cast<uInt>(srcLen);

  out.reserve(static_cast<std::size_t>(
      std::min(expectedLen, srcLen * MAX_DEFLATE_RATIO + 64)));

  uint8_t buf[16384];
  int ret = Z_OK;
  do {
    zs.next_out = buf;
    zs.avail_out = sizeof(buf);
    ret = inflate(&zs, Z_NO_FLUSH);
    if (ret != Z_OK && ret != Z_STREAM_END) {
      inflateEnd(&zs);
      err = "zlib inflate error (corrupt or truncated data)";
      return false;
    }
    const std::size_t produced = sizeof(buf) - zs.avail_out;
    // Invariant: out.size() <= expectedLen, so the subtraction cannot wrap.
    if (produced > expectedLen - out.size()) {
      inflateEnd(&zs);
      err = "ZIP entry expands beyond its declared size";
      return false;
    }
    out.insert(out.end(), buf, buf + produced);
  } while (ret != Z_STREAM_END);

  inflateEnd(&zs);
  if (out.size() != expectedLen) {
    err = "ZIP entry is smaller than its declared size";
    return false;
  }
  return true;
}

template <typename Buffer>
bool decodePayload(uint16_t method, uint64_t uncompressedSize,
                   const std::vector<uint8_t> &compressed,
                   const std::string &name, Buffer &out, std::string &err) {
  if (method == 0) {
    if (compressed.size() != uncompressedSize) {
      err = "Corrupt stored ZIP entry '" + name + "' (size mismatch)";
      return false;
    }
    out.assign(compressed.begin(), compressed.end());
    return true;
  }
  if (method == 8) {
    return inflateRaw(compressed.data(), compressed.size(), uncompressedSize,
                      out, err);
  }
  err = "Unsupported ZIP compression method for '" + name + "'";
  return false;
}

} // namespace

bool ZipReader::open(const std::string &path, std::string &err) {
  path_.clear();
  fileSize_ = 0;
  stream_.close();
  stream_.clear();
  entryOrder_.clear();
  entries_.clear();

  stream_.open(path, std::ios::binary | std::ios::ate);
  if (!stream_) {
    err = "Cannot open " + path;
    return false;
  }
  std::streampos szPos = stream_.tellg();
  if (szPos < 22) {
    err = "Invalid ZIP archive: file too small";
    return false;
  }
  uint64_t fileSize = static_cast<uint64_t>(szPos);
  if (!parseCentralDirectory(fileSize, err)) {
    stream_.close();
    return false;
  }
  path_ = path;
  fileSize_ = fileSize;
  return true;
}

bool ZipReader::hasEntry(const std::string &name) const {
  return entries_.find(name) != entries_.end();
}

bool ZipReader::parseCentralDirectory(uint64_t fileSize, std::string &err) {
  std::size_t tailSize = static_cast<std::size_t>(
      std::min<uint64_t>(fileSize, 65536u + 22u + 20u));
  std::vector<uint8_t> tail(tailSize);
  stream_.clear();
  stream_.seekg(static_cast<std::streamoff>(fileSize - tailSize), std::ios::beg);
  if (!stream_.read(reinterpret_cast<char *>(tail.data()),
                    static_cast<std::streamsize>(tailSize))) {
    err = "Failed reading ZIP trailer";
    return false;
  }

  std::size_t eocd = std::string::npos;
  for (std::size_t i = tailSize - 22;; --i) {
    if (readU32(tail.data() + i) == 0x06054b50u) {
      uint16_t commentLen = readU16(tail.data() + i + 20);
      if (i + 22 + commentLen == tailSize) {
        eocd = i;
        break;
      }
    }
    if (i == 0) {
      break;
    }
  }
  if (eocd == std::string::npos) {
    err = "Invalid ZIP archive: EOCD not found";
    return false;
  }

  uint64_t totalEntries = readU16(tail.data() + eocd + 10);
  uint64_t cdSize = readU32(tail.data() + eocd + 12);
  uint64_t cdOffset = readU32(tail.data() + eocd + 16);

  // Check for ZIP64 End of Central Directory Locator immediately before EOCD
  if ((totalEntries == 0xFFFFu || cdSize == 0xFFFFFFFFu ||
       cdOffset == 0xFFFFFFFFu) &&
      eocd >= 20 && readU32(tail.data() + eocd - 20) == 0x07064b50u) {
    uint64_t eocd64Off = readU64(tail.data() + eocd - 20 + 8);
    if (!rangeWithin(eocd64Off, 56, fileSize)) {
      err = "Invalid ZIP64 EOCD offset";
      return false;
    }
    uint8_t eocd64[56];
    stream_.clear();
    stream_.seekg(static_cast<std::streamoff>(eocd64Off), std::ios::beg);
    if (!stream_.read(reinterpret_cast<char *>(eocd64), 56) ||
        readU32(eocd64) != 0x06064b50u) {
      err = "Invalid ZIP64 EOCD record";
      return false;
    }
    totalEntries = readU64(eocd64 + 32);
    cdSize = readU64(eocd64 + 40);
    cdOffset = readU64(eocd64 + 48);
  }

  if (!rangeWithin(cdOffset, cdSize, fileSize)) {
    err = "Invalid ZIP archive: central directory out of range";
    return false;
  }

  std::vector<uint8_t> cdBuf(static_cast<std::size_t>(cdSize));
  if (cdSize > 0) {
    stream_.clear();
    stream_.seekg(static_cast<std::streamoff>(cdOffset), std::ios::beg);
    if (!stream_.read(reinterpret_cast<char *>(cdBuf.data()),
                      static_cast<std::streamsize>(cdSize))) {
      err = "Failed reading ZIP central directory";
      return false;
    }
  }

  entryOrder_.reserve(static_cast<std::size_t>(
      std::min<uint64_t>(totalEntries, 65536u)));
  std::size_t pos = 0;
  for (uint64_t idx = 0; idx < totalEntries; ++idx) {
    if (pos + 46 > cdBuf.size() || readU32(cdBuf.data() + pos) != 0x02014b50u) {
      err = "Invalid ZIP archive: bad central directory entry";
      return false;
    }
    const uint8_t *cd = cdBuf.data() + pos;
    ZipEntry ze;
    ze.method = readU16(cd + 10);
    ze.compressedSize = readU32(cd + 20);
    ze.uncompressedSize = readU32(cd + 24);
    uint16_t nameLen = readU16(cd + 28);
    uint16_t extraLen = readU16(cd + 30);
    uint16_t commentLen = readU16(cd + 32);
    ze.localHeaderOffset = readU32(cd + 42);

    if (pos + 46 + nameLen + extraLen + commentLen > cdBuf.size()) {
      err = "Invalid ZIP archive: truncated entry metadata";
      return false;
    }

    // Parse ZIP64 extra field (0x0001) if any 32-bit field saturated
    if (ze.uncompressedSize == 0xFFFFFFFFu ||
        ze.compressedSize == 0xFFFFFFFFu ||
        ze.localHeaderOffset == 0xFFFFFFFFu) {
      const uint8_t *extra = cd + 46 + nameLen;
      std::size_t epos = 0;
      while (epos + 4 <= extraLen) {
        uint16_t hdrId = readU16(extra + epos);
        uint16_t dataSz = readU16(extra + epos + 2);
        epos += 4;
        if (epos + dataSz > extraLen) {
          break;
        }
        if (hdrId == 0x0001u) {
          std::size_t zoff = 0;
          if (ze.uncompressedSize == 0xFFFFFFFFu && zoff + 8 <= dataSz) {
            ze.uncompressedSize = readU64(extra + epos + zoff);
            zoff += 8;
          }
          if (ze.compressedSize == 0xFFFFFFFFu && zoff + 8 <= dataSz) {
            ze.compressedSize = readU64(extra + epos + zoff);
            zoff += 8;
          }
          if (ze.localHeaderOffset == 0xFFFFFFFFu && zoff + 8 <= dataSz) {
            ze.localHeaderOffset = readU64(extra + epos + zoff);
            zoff += 8;
          }
          break;
        }
        epos += dataSz;
      }
    }

    std::string name(reinterpret_cast<const char *>(cd + 46), nameLen);
    if (entries_.emplace(name, ze).second) {
      entryOrder_.push_back(std::move(name));
    }
    pos += 46 + nameLen + extraLen + commentLen;
  }
  return true;
}

bool ZipReader::readCompressedEntry(const std::string &name, ZipEntry &meta,
                                    std::vector<uint8_t> &compressed,
                                    std::string &err) const {
  auto it = entries_.find(name);
  if (it == entries_.end()) {
    err = "Missing '" + name + "' in archive";
    return false;
  }
  meta = it->second;

  // Validate header-declared sizes before they drive any allocation.
  if (meta.compressedSize > MAX_ENTRY_BYTES ||
      meta.uncompressedSize > MAX_ENTRY_BYTES) {
    err = "ZIP entry '" + name + "' is too large";
    return false;
  }

  if (!stream_.is_open()) {
    stream_.clear();
    stream_.open(path_, std::ios::binary);
    if (!stream_) {
      err = "Cannot reopen " + path_;
      return false;
    }
  }
  if (!rangeWithin(meta.localHeaderOffset, 30, fileSize_)) {
    err = "Corrupt ZIP local header for '" + name + "'";
    return false;
  }
  uint8_t lh[30];
  stream_.clear();
  stream_.seekg(static_cast<std::streamoff>(meta.localHeaderOffset),
                std::ios::beg);
  if (!stream_.read(reinterpret_cast<char *>(lh), 30)) {
    err = "Corrupt ZIP local header for '" + name + "'";
    return false;
  }
  if (readU32(lh) != 0x04034b50u) {
    err = "Invalid ZIP local signature for '" + name + "'";
    return false;
  }

  uint16_t nameLen = readU16(lh + 26);
  uint16_t extraLen = readU16(lh + 28);
  // localHeaderOffset <= fileSize_ was checked above, so this cannot wrap.
  uint64_t payloadOffset = meta.localHeaderOffset + 30 + nameLen + extraLen;
  if (!rangeWithin(payloadOffset, meta.compressedSize, fileSize_)) {
    err = "Truncated ZIP payload for '" + name + "'";
    return false;
  }
  compressed.resize(static_cast<std::size_t>(meta.compressedSize));
  if (meta.compressedSize > 0) {
    stream_.clear();
    stream_.seekg(static_cast<std::streamoff>(payloadOffset), std::ios::beg);
    if (!stream_.read(reinterpret_cast<char *>(compressed.data()),
                      static_cast<std::streamsize>(meta.compressedSize))) {
      err = "Truncated ZIP payload for '" + name + "'";
      return false;
    }
  }
  return true;
}

bool ZipReader::readEntry(const std::string &name, std::string &out,
                          std::string &err) const {
  ZipEntry meta;
  std::vector<uint8_t> compressed;
  if (!readCompressedEntry(name, meta, compressed, err)) {
    return false;
  }
  return decodePayload(meta.method, meta.uncompressedSize, compressed, name,
                       out, err);
}

bool ZipReader::readEntryBytes(const std::string &name,
                               std::vector<uint8_t> &out,
                               std::string &err) const {
  ZipEntry meta;
  std::vector<uint8_t> compressed;
  if (!readCompressedEntry(name, meta, compressed, err)) {
    return false;
  }
  return decodePayload(meta.method, meta.uncompressedSize, compressed, name,
                       out, err);
}

} // namespace zip_util
