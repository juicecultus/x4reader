#pragma once

#include <Arduino.h>
#include <SD.h>

#include <cstdint>
#include <string>
#include <vector>

class XtcFile {
 public:
  struct PageInfo {
    uint32_t offset;
    uint32_t size;
    uint16_t width;
    uint16_t height;
    uint8_t bitDepth;
  };

  XtcFile();
  ~XtcFile();

  bool open(const String& path);
  void close();
  bool isOpen() const { return isOpen_; }

  const String& getPath() const { return path_; }
  uint16_t getPageCount() const { return pageCount_; }
  uint16_t getWidth() const { return defaultWidth_; }
  uint16_t getHeight() const { return defaultHeight_; }
  uint8_t getBitDepth() const { return bitDepth_; }

  bool getPageInfo(uint32_t pageIndex, PageInfo& out) const;

  size_t loadPage(uint32_t pageIndex, uint8_t* buffer, size_t bufferSize);

  // Streaming helpers (avoid full-page allocations)
  // Seeks to page data and validates the XTG/XTH header.
  // Returns the bitmap data offset (immediately after the page header) and dimensions.
  bool getPageBitmapOffset(uint32_t pageIndex, uint32_t& outBitmapOffset, uint16_t& outWidth, uint16_t& outHeight);

  // Read raw bytes from the underlying file at an absolute offset.
  // Returns bytes read (0 on failure).
  size_t readAt(uint32_t offset, uint8_t* buffer, size_t len);

  static bool isXtcExtension(const String& path);

 private:
  struct Header {
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

  struct PageTableEntry {
    uint64_t dataOffset;
    uint32_t dataSize;
    uint16_t width;
    uint16_t height;
  };

  struct PageHeader {
    uint32_t magic;
    uint16_t width;
    uint16_t height;
    uint8_t colorMode;
    uint8_t compression;
    uint32_t dataSize;
    uint64_t md5;
  };

  static constexpr uint32_t XTC_MAGIC = 0x00435458;   // "XTC\0"
  static constexpr uint32_t XTCH_MAGIC = 0x48435458;  // "XTCH"
  static constexpr uint32_t XTG_MAGIC = 0x00475458;   // "XTG\0"
  static constexpr uint32_t XTH_MAGIC = 0x00485458;   // "XTH\0"

  bool readHeader();
  bool readPageTable();

  String path_;
  File file_;
  bool isOpen_ = false;

  Header header_{};
  uint16_t pageCount_ = 0;
  uint16_t defaultWidth_ = 480;
  uint16_t defaultHeight_ = 800;
  uint8_t bitDepth_ = 1;

  std::vector<PageInfo> pages_;
};
