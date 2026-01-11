#include "XtcViewerScreen.h"

#include <resources/fonts/FontDefinitions.h>

#include <cstring>

#include "../../core/Buttons.h"
#include "../../core/Settings.h"

static uint32_t fnv1a32(const char* s) {
  uint32_t h = 2166136261u;
  if (!s) {
    return h;
  }
  while (*s) {
    h ^= (uint8_t)(*s++);
    h *= 16777619u;
  }
  return h;
}

static bool writeBmp24TopDown(const char* path, uint16_t w, uint16_t h,
                             std::function<void(uint16_t x, uint16_t y, uint8_t& r, uint8_t& g, uint8_t& b)> getPixel) {
  if (!path) {
    return false;
  }

  if (SD.exists(path)) {
    SD.remove(path);
  }

  File f = SD.open(path, FILE_WRITE);
  if (!f) {
    return false;
  }

  const uint32_t rowStride = ((uint32_t)w * 3u + 3u) & ~3u;
  const uint32_t pixelDataSize = rowStride * (uint32_t)h;
  const uint32_t fileSize = 54u + pixelDataSize;

  uint8_t hdr[54];
  memset(hdr, 0, sizeof(hdr));
  hdr[0] = 'B';
  hdr[1] = 'M';
  hdr[2] = (uint8_t)(fileSize & 0xFF);
  hdr[3] = (uint8_t)((fileSize >> 8) & 0xFF);
  hdr[4] = (uint8_t)((fileSize >> 16) & 0xFF);
  hdr[5] = (uint8_t)((fileSize >> 24) & 0xFF);
  hdr[10] = 54;
  hdr[14] = 40;
  hdr[18] = (uint8_t)(w & 0xFF);
  hdr[19] = (uint8_t)((w >> 8) & 0xFF);
  int32_t negH = -(int32_t)h;
  hdr[22] = (uint8_t)(negH & 0xFF);
  hdr[23] = (uint8_t)((negH >> 8) & 0xFF);
  hdr[24] = (uint8_t)((negH >> 16) & 0xFF);
  hdr[25] = (uint8_t)((negH >> 24) & 0xFF);
  hdr[26] = 1;
  hdr[28] = 24;
  hdr[34] = (uint8_t)(pixelDataSize & 0xFF);
  hdr[35] = (uint8_t)((pixelDataSize >> 8) & 0xFF);
  hdr[36] = (uint8_t)((pixelDataSize >> 16) & 0xFF);
  hdr[37] = (uint8_t)((pixelDataSize >> 24) & 0xFF);

  if (f.write(hdr, sizeof(hdr)) != sizeof(hdr)) {
    f.close();
    return false;
  }

  uint8_t* row = (uint8_t*)malloc(rowStride);
  if (!row) {
    f.close();
    return false;
  }

  for (uint16_t y = 0; y < h; ++y) {
    uint32_t idx = 0;
    for (uint16_t x = 0; x < w; ++x) {
      uint8_t r = 255, g = 255, b = 255;
      getPixel(x, y, r, g, b);
      row[idx++] = b;
      row[idx++] = g;
      row[idx++] = r;
    }
    while (idx < rowStride) {
      row[idx++] = 0;
    }
    if (f.write(row, rowStride) != rowStride) {
      free(row);
      f.close();
      return false;
    }
  }

  free(row);
  f.close();
  return true;
}

static inline bool getFrameBufferPixelBw(const uint8_t* fb, int fx, int fy) {
  // Returns true if pixel is white, false if black.
  if (!fb) {
    return true;
  }
  if (fx < 0 || fx >= 800 || fy < 0 || fy >= 480) {
    return true;
  }
  const int byteIdx = (fy * 100) + (fx / 8);
  const int bitIdx = 7 - (fx % 8);
  return ((fb[byteIdx] >> bitIdx) & 1) != 0;
}

XtcViewerScreen::XtcViewerScreen(EInkDisplay& display, TextRenderer& renderer, SDCardManager& sdManager, UIManager& uiManager)
    : display(display), textRenderer(renderer), sdManager(sdManager), uiManager(uiManager) {}

void XtcViewerScreen::begin() {
  loadSettingsFromFile();
}

void XtcViewerScreen::activate() {
  if (pendingOpenPath.length() > 0) {
    String p = pendingOpenPath;
    pendingOpenPath = String("");
    openFile(p);
  }
}

void XtcViewerScreen::show() {
  renderPage();
}

void XtcViewerScreen::handleButtons(Buttons& buttons) {
  if (!valid) {
    if (buttons.isPressed(Buttons::BACK)) {
      uiManager.showScreen(UIManager::ScreenId::FileBrowser);
    }
    return;
  }

  if (buttons.isPressed(Buttons::BACK)) {
    uiManager.showScreen(UIManager::ScreenId::FileBrowser);
  } else if (buttons.isPressed(Buttons::LEFT)) {
    nextPage();
  } else if (buttons.isPressed(Buttons::RIGHT)) {
    prevPage();
  }
}

void XtcViewerScreen::shutdown() {
  savePositionToFile();
  saveSettingsToFile();
}

void XtcViewerScreen::openFile(const String& sdPath) {
  if (!sdManager.ready()) {
    valid = false;
    return;
  }

  closeDocument();

  currentFilePath = sdPath;

  loadPositionFromFile();

  sdManager.ensureSpiBusIdle();
  valid = xtc.open(sdPath);
  if (!valid) {
    return;
  }

  const uint32_t coverKey = fnv1a32(sdPath.c_str());
  (void)sdManager.ensureDirectoryExists("/microreader/xtc_covers");
  String coverPath = String("/microreader/xtc_covers/") + String(coverKey, HEX) + String(".bmp");

  // Generate sleep cover by rendering page 0 to the framebuffer (streaming) and writing BMP from framebuffer.
  {
    const uint32_t prevPage = currentPage;
    currentPage = 0;
    renderPage();
    currentPage = prevPage;

    const uint8_t* fb = display.getFrameBuffer();
    const uint16_t w = xtc.getWidth();
    const uint16_t h = xtc.getHeight();
    (void)writeBmp24TopDown(
        coverPath.c_str(), w, h,
        [&](uint16_t x, uint16_t y, uint8_t& r, uint8_t& g, uint8_t& b) {
          const int fx = (int)y;
          const int fy = 479 - (int)x;
          const bool isWhite = getFrameBufferPixelBw(fb, fx, fy);
          const uint8_t v = isWhite ? 255 : 0;
          r = v;
          g = v;
          b = v;
        });

    Settings& s = uiManager.getSettings();
    s.setString(String("textviewer.lastCoverPath"), coverPath);
    (void)s.save();
  }

  saveSettingsToFile();
}

void XtcViewerScreen::closeDocument() {
  xtc.close();
  valid = false;
  currentPage = 0;
  currentFilePath = String("");
}

void XtcViewerScreen::renderPage() {
  display.clearScreen(0xFF);
  textRenderer.setFrameBuffer(display.getFrameBuffer());
  textRenderer.setBitmapType(TextRenderer::BITMAP_BW);

  if (!valid) {
    textRenderer.setTextColor(TextRenderer::COLOR_BLACK);
    textRenderer.setFont(getMainFont());
    textRenderer.setCursor(20, 200);
    textRenderer.print("No document");
    display.displayBuffer(EInkDisplay::FAST_REFRESH);
    return;
  }

  const uint16_t w = xtc.getWidth();
  const uint16_t h = xtc.getHeight();
  const uint8_t bd = xtc.getBitDepth();

  uint32_t bitmapOffset = 0;
  uint16_t pw = 0;
  uint16_t ph = 0;
  if (!xtc.getPageBitmapOffset(currentPage, bitmapOffset, pw, ph)) {
    textRenderer.setTextColor(TextRenderer::COLOR_BLACK);
    textRenderer.setFont(getMainFont());
    textRenderer.setCursor(20, 200);
    textRenderer.print("Page load error");
    display.displayBuffer(EInkDisplay::FAST_REFRESH);
    return;
  }

  uint8_t* fb = display.getFrameBuffer();

  if (bd == 2) {

    const size_t colBytes = (h + 7) / 8;
    const size_t planeSize = (static_cast<size_t>(w) * h + 7) / 8;
    uint8_t* col1 = (uint8_t*)malloc(colBytes);
    uint8_t* col2 = (uint8_t*)malloc(colBytes);
    if (!col1 || !col2) {
      if (col1) free(col1);
      if (col2) free(col2);
      textRenderer.setTextColor(TextRenderer::COLOR_BLACK);
      textRenderer.setFont(getMainFont());
      textRenderer.setCursor(20, 200);
      textRenderer.print("Memory error");
      display.displayBuffer(EInkDisplay::FAST_REFRESH);
      return;
    }

    for (uint16_t x = 0; x < w; ++x) {
      const size_t colIndex = (size_t)w - 1 - x;
      const uint32_t off1 = bitmapOffset + (uint32_t)(colIndex * colBytes);
      const uint32_t off2 = bitmapOffset + (uint32_t)planeSize + (uint32_t)(colIndex * colBytes);
      if (xtc.readAt(off1, col1, colBytes) != colBytes || xtc.readAt(off2, col2, colBytes) != colBytes) {
        continue;
      }

      for (uint16_t y = 0; y < h; ++y) {
        const size_t byteInCol = y / 8;
        const size_t bitInByte = 7 - (y % 8);
        const uint8_t bit1 = (col1[byteInCol] >> bitInByte) & 1;
        const uint8_t bit2 = (col2[byteInCol] >> bitInByte) & 1;
        const uint8_t pv = (bit1 << 1) | bit2;

        uint8_t lum = 255;
        if (pv == 1) lum = 96;
        else if (pv == 2) lum = 192;
        else if (pv == 3) lum = 0;

        const bool black = (lum < 128);
        const int fx = (int)y;
        const int fy = 479 - (int)x;
        if (fx < 0 || fx >= 800 || fy < 0 || fy >= 480) {
          continue;
        }
        const int byteIdx = (fy * 100) + (fx / 8);
        const int bitIdx = 7 - (fx % 8);
        if (black) {
          fb[byteIdx] &= ~(1 << bitIdx);
        } else {
          fb[byteIdx] |= (1 << bitIdx);
        }
      }
    }

    free(col1);
    free(col2);

  } else {
    const size_t rowBytes = (w + 7) / 8;
    uint8_t* row = (uint8_t*)malloc(rowBytes);
    if (!row) {
      textRenderer.setTextColor(TextRenderer::COLOR_BLACK);
      textRenderer.setFont(getMainFont());
      textRenderer.setCursor(20, 200);
      textRenderer.print("Memory error");
      display.displayBuffer(EInkDisplay::FAST_REFRESH);
      return;
    }

    for (uint16_t y = 0; y < h; ++y) {
      const uint32_t off = bitmapOffset + (uint32_t)((size_t)y * rowBytes);
      if (xtc.readAt(off, row, rowBytes) != rowBytes) {
        continue;
      }
      for (uint16_t x = 0; x < w; ++x) {
        const size_t byteIdx = x / 8;
        const size_t bitIdx = 7 - (x % 8);
        const bool isBlack = !((row[byteIdx] >> bitIdx) & 1);

        const int fx = (int)y;
        const int fy = 479 - (int)x;
        if (fx < 0 || fx >= 800 || fy < 0 || fy >= 480) {
          continue;
        }
        const int fbByte = (fy * 100) + (fx / 8);
        const int fbBit = 7 - (fx % 8);
        if (isBlack) {
          fb[fbByte] &= ~(1 << fbBit);
        } else {
          fb[fbByte] |= (1 << fbBit);
        }
      }
    }

    free(row);
  }

  display.displayBuffer(EInkDisplay::FAST_REFRESH);
}

void XtcViewerScreen::nextPage() {
  if (!valid) {
    return;
  }
  const uint16_t n = xtc.getPageCount();
  if (currentPage + 1 < n) {
    currentPage++;
    show();
  }
}

void XtcViewerScreen::prevPage() {
  if (!valid) {
    return;
  }
  if (currentPage > 0) {
    currentPage--;
    show();
  }
}

void XtcViewerScreen::saveSettingsToFile() {
  Settings& s = uiManager.getSettings();
  if (currentFilePath.length() > 0) {
    s.setString(String("xtcviewer.lastPath"), currentFilePath);
  }
  (void)s.save();
}

void XtcViewerScreen::loadSettingsFromFile() {
  Settings& s = uiManager.getSettings();
  String saved = s.getString(String("xtcviewer.lastPath"), String(""));
  if (saved.length() > 0) {
    pendingOpenPath = saved;
  }
}

void XtcViewerScreen::savePositionToFile() {
  if (!valid || currentFilePath.length() == 0) {
    return;
  }
  String posPath = currentFilePath + String(".xtcpos");
  String content = String(currentPage);
  (void)sdManager.writeFile(posPath.c_str(), content);
}

void XtcViewerScreen::loadPositionFromFile() {
  currentPage = 0;
  if (currentFilePath.length() == 0) {
    return;
  }

  String posPath = currentFilePath + String(".xtcpos");
  sdManager.ensureSpiBusIdle();
  if (!SD.exists(posPath.c_str())) {
    return;
  }

  char buf[32];
  size_t r = sdManager.readFileToBuffer(posPath.c_str(), buf, sizeof(buf));
  if (r > 0) {
    currentPage = (uint32_t)atoi(buf);
  }
}
