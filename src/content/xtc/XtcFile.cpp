#include "XtcFile.h"

#include <cstring>

#pragma pack(push, 1)
struct XtcHeaderPacked {
  uint32_t magic;
  uint8_t versionMajor;
  uint8_t versionMinor;
  uint16_t pageCount;
  uint32_t flags;
  uint32_t headerSize;
  uint32_t reserved1;
  uint32_t tocOffset;
  uint64_t pageTableOffset;
  uint64_t dataOffset;
  uint64_t reserved2;
  uint32_t titleOffset;
  uint32_t padding;
};

struct XtcPageTableEntryPacked {
  uint64_t dataOffset;
  uint32_t dataSize;
  uint16_t width;
  uint16_t height;
};

struct XtcPageHeaderPacked {
  uint32_t magic;
  uint16_t width;
  uint16_t height;
  uint8_t colorMode;
  uint8_t compression;
  uint32_t dataSize;
  uint64_t md5;
};
#pragma pack(pop)

XtcFile::XtcFile() = default;

XtcFile::~XtcFile() { close(); }

bool XtcFile::isXtcExtension(const String& path) {
  if (path.length() < 4) {
    return false;
  }
  String lf = path;
  lf.toLowerCase();
  return lf.endsWith(".xtc") || lf.endsWith(".xtch");
}

bool XtcFile::open(const String& path) {
  close();

  if (path.length() == 0) {
    return false;
  }

  if (!SD.exists(path.c_str())) {
    return false;
  }

  file_ = SD.open(path.c_str(), FILE_READ);
  if (!file_) {
    return false;
  }

  path_ = path;

  if (!readHeader()) {
    close();
    return false;
  }

  if (!readPageTable()) {
    close();
    return false;
  }

  isOpen_ = true;
  return true;
}

void XtcFile::close() {
  if (file_) {
    file_.close();
  }
  isOpen_ = false;
  path_ = String("");
  memset(&header_, 0, sizeof(header_));
  pageCount_ = 0;
  defaultWidth_ = 480;
  defaultHeight_ = 800;
  bitDepth_ = 1;
  pages_.clear();
}

bool XtcFile::readHeader() {
  if (!file_) {
    return false;
  }

  if (!file_.seek(0)) {
    return false;
  }

  XtcHeaderPacked hdr{};
  const size_t want = sizeof(hdr);
  const size_t got = file_.read(reinterpret_cast<uint8_t*>(&hdr), want);
  if (got != want) {
    return false;
  }

  if (hdr.magic != XTC_MAGIC && hdr.magic != XTCH_MAGIC) {
    return false;
  }

  const bool validVersion = (hdr.versionMajor == 1 && hdr.versionMinor == 0) || (hdr.versionMajor == 0 && hdr.versionMinor == 1);
  if (!validVersion) {
    return false;
  }

  if (hdr.pageCount == 0) {
    return false;
  }

  header_.magic = hdr.magic;
  header_.versionMajor = hdr.versionMajor;
  header_.versionMinor = hdr.versionMinor;
  header_.pageCount = hdr.pageCount;
  header_.flags = hdr.flags;
  header_.headerSize = hdr.headerSize;
  header_.reserved1 = hdr.reserved1;
  header_.tocOffset = hdr.tocOffset;
  header_.pageTableOffset = hdr.pageTableOffset;
  header_.dataOffset = hdr.dataOffset;
  header_.reserved2 = hdr.reserved2;
  header_.titleOffset = hdr.titleOffset;
  header_.padding = hdr.padding;

  pageCount_ = header_.pageCount;
  bitDepth_ = (header_.magic == XTCH_MAGIC) ? 2 : 1;
  return true;
}

bool XtcFile::readPageTable() {
  if (!file_) {
    return false;
  }

  if (header_.pageTableOffset == 0) {
    return false;
  }

  if (!file_.seek(static_cast<uint32_t>(header_.pageTableOffset))) {
    return false;
  }

  pages_.clear();
  pages_.reserve(pageCount_);

  for (uint16_t i = 0; i < pageCount_; ++i) {
    XtcPageTableEntryPacked ent{};
    const size_t want = sizeof(ent);
    const size_t got = file_.read(reinterpret_cast<uint8_t*>(&ent), want);
    if (got != want) {
      return false;
    }

    PageInfo info{};
    info.offset = static_cast<uint32_t>(ent.dataOffset);
    info.size = ent.dataSize;
    info.width = ent.width;
    info.height = ent.height;
    info.bitDepth = bitDepth_;
    pages_.push_back(info);

    if (i == 0) {
      defaultWidth_ = ent.width;
      defaultHeight_ = ent.height;
    }
  }

  return true;
}

bool XtcFile::getPageInfo(uint32_t pageIndex, PageInfo& out) const {
  if (pageIndex >= pages_.size()) {
    return false;
  }
  out = pages_[pageIndex];
  return true;
}

size_t XtcFile::loadPage(uint32_t pageIndex, uint8_t* buffer, size_t bufferSize) {
  if (!file_ || !isOpen_) {
    return 0;
  }

  if (pageIndex >= pages_.size()) {
    return 0;
  }

  const PageInfo& page = pages_[pageIndex];
  if (!file_.seek(page.offset)) {
    return 0;
  }

  XtcPageHeaderPacked ph{};
  const size_t wantHeader = sizeof(ph);
  const size_t gotHeader = file_.read(reinterpret_cast<uint8_t*>(&ph), wantHeader);
  if (gotHeader != wantHeader) {
    return 0;
  }

  const uint32_t expected = (bitDepth_ == 2) ? XTH_MAGIC : XTG_MAGIC;
  if (ph.magic != expected) {
    return 0;
  }

  size_t bitmapSize = 0;
  if (bitDepth_ == 2) {
    bitmapSize = ((static_cast<size_t>(ph.width) * ph.height + 7) / 8) * 2;
  } else {
    bitmapSize = ((ph.width + 7) / 8) * ph.height;
  }

  if (bufferSize < bitmapSize) {
    return 0;
  }

  const size_t got = file_.read(buffer, bitmapSize);
  if (got != bitmapSize) {
    return 0;
  }

  return got;
}

size_t XtcFile::readAt(uint32_t offset, uint8_t* buffer, size_t len) {
  if (!file_ || !isOpen_ || !buffer || len == 0) {
    return 0;
  }
  if (!file_.seek(offset)) {
    return 0;
  }
  return file_.read(buffer, len);
}

bool XtcFile::getPageBitmapOffset(uint32_t pageIndex, uint32_t& outBitmapOffset, uint16_t& outWidth, uint16_t& outHeight) {
  outBitmapOffset = 0;
  outWidth = 0;
  outHeight = 0;

  if (!file_ || !isOpen_) {
    return false;
  }
  if (pageIndex >= pages_.size()) {
    return false;
  }

  const PageInfo& page = pages_[pageIndex];
  if (!file_.seek(page.offset)) {
    return false;
  }

  XtcPageHeaderPacked ph{};
  const size_t wantHeader = sizeof(ph);
  const size_t gotHeader = file_.read(reinterpret_cast<uint8_t*>(&ph), wantHeader);
  if (gotHeader != wantHeader) {
    return false;
  }

  const uint32_t expected = (bitDepth_ == 2) ? XTH_MAGIC : XTG_MAGIC;
  if (ph.magic != expected) {
    return false;
  }

  outWidth = ph.width;
  outHeight = ph.height;
  outBitmapOffset = page.offset + (uint32_t)sizeof(ph);
  return true;
}
