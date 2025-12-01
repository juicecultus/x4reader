#include "TextRenderer.h"

#include <cstring>

#include "../core/EInkDisplay.h"
#include "SimpleFont.h"

static constexpr int GLYPH_PADDING = 0;
static constexpr uint32_t UTF8_REPLACEMENT_CHAR = 0xFFFD;
static constexpr uint16_t FALLBACK_GLYPH_WIDTH = 6;

// Helper function to decode a single UTF-8 codepoint from a byte sequence
// Returns the decoded codepoint and advances the pointer
static uint32_t decodeUtf8Codepoint(const unsigned char*& p) {
  if (!p || !*p) {
    return 0;
  }

  unsigned char c = *p;

  // 1-byte ASCII
  if (c < 0x80) {
    p += 1;
    return c;
  }

  // 2-byte sequence
  if ((c & 0xE0) == 0xC0) {
    if (p[1] && (p[1] & 0xC0) == 0x80) {
      uint32_t cp = ((c & 0x1F) << 6) | (p[1] & 0x3F);
      p += 2;
      return cp;
    }
    p += 1;
    return UTF8_REPLACEMENT_CHAR;
  }

  // 3-byte sequence
  if ((c & 0xF0) == 0xE0) {
    if (p[1] && p[2] && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
      uint32_t cp = ((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
      p += 3;
      return cp;
    }
    p += 1;
    return UTF8_REPLACEMENT_CHAR;
  }

  // 4-byte sequence
  if ((c & 0xF8) == 0xF0) {
    if (p[1] && p[2] && p[3] && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) {
      uint32_t cp = ((c & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
      p += 4;
      return cp;
    }
    p += 1;
    return UTF8_REPLACEMENT_CHAR;
  }

  // Invalid leading byte
  p += 1;
  return UTF8_REPLACEMENT_CHAR;
}

// Helper function to find a glyph index by codepoint
static int findGlyphIndex(const SimpleGFXfont* font, uint32_t codepoint) {
  if (!font) {
    return -1;
  }

  for (uint16_t i = 0; i < font->glyphCount; ++i) {
    if (font->glyph[i].codepoint == codepoint) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

TextRenderer::TextRenderer(EInkDisplay& display) : display(display) {
  Serial.printf("[%lu] TextRenderer: Constructor called\n", millis());
}

void TextRenderer::drawPixel(int16_t x, int16_t y, uint16_t color) {
  // Bounds checking (portrait: 480x800)
  if (x < 0 || x >= EInkDisplay::DISPLAY_HEIGHT || y < 0 || y >= EInkDisplay::DISPLAY_WIDTH) {
    return;
  }

  // Rotate coordinates: portrait (480x800) -> landscape (800x480)
  // Rotation: 90 degrees clockwise
  int16_t rotatedX = y;
  int16_t rotatedY = EInkDisplay::DISPLAY_HEIGHT - 1 - x;

  // Get frame buffer
  uint8_t* frameBuffer = display.getFrameBuffer();

  // Calculate byte position and bit position
  uint16_t byteIndex = rotatedY * EInkDisplay::DISPLAY_WIDTH_BYTES + (rotatedX / 8);
  uint8_t bitPosition = 7 - (rotatedX % 8);  // MSB first

  // Set or clear the bit (0 = black, 1 = white in e-ink)
  if (color == COLOR_BLACK) {
    frameBuffer[byteIndex] &= ~(1 << bitPosition);  // Clear bit (black)
  } else {
    frameBuffer[byteIndex] |= (1 << bitPosition);  // Set bit (white)
  }
}

void TextRenderer::drawPixelGray(int16_t x, int16_t y, bool bw, bool lsb, bool msb) {
  // Bounds checking (portrait: 480x800)
  if (x < 0 || x >= EInkDisplay::DISPLAY_HEIGHT || y < 0 || y >= EInkDisplay::DISPLAY_WIDTH) {
    return;
  }

  // Rotate coordinates: portrait (480x800) -> landscape (800x480)
  // Rotation: 90 degrees clockwise
  int16_t rotatedX = y;
  int16_t rotatedY = EInkDisplay::DISPLAY_HEIGHT - 1 - x;

  // Get buffers
  uint8_t* bwBuffer = display.getFrameBuffer();
  uint8_t* lsbBuffer = display.getFrameBufferLSB();
  uint8_t* msbBuffer = display.getFrameBufferMSB();

  // Calculate byte position and bit position
  uint16_t byteIndex = rotatedY * EInkDisplay::DISPLAY_WIDTH_BYTES + (rotatedX / 8);
  uint8_t bitPosition = 7 - (rotatedX % 8);  // MSB first

  // Set BW buffer
  if (bw) {
    bwBuffer[byteIndex] &= ~(1 << bitPosition);
  }

  if (lsb || msb) {
    // Set LSB buffer
    if (lsb) {
      lsbBuffer[byteIndex] &= ~(1 << bitPosition);
    } else {
      lsbBuffer[byteIndex] |= (1 << bitPosition);
    }

    // Set MSB buffer
    if (msb) {
      msbBuffer[byteIndex] &= ~(1 << bitPosition);
    } else {
      msbBuffer[byteIndex] |= (1 << bitPosition);
    }
  }
}

void TextRenderer::setFont(const SimpleGFXfont* f) {
  currentFont = f;
}

void TextRenderer::setTextColor(uint16_t c) {
  textColor = c;
}

void TextRenderer::setGrayscaleMode(bool enable) {
  grayscaleMode = enable;
}

void TextRenderer::setCursor(int16_t x, int16_t y) {
  cursorX = x;
  cursorY = y;
}

size_t TextRenderer::print(const char* s) {
  if (!s) {
    return 0;
  }

  size_t written = 0;
  const unsigned char* p = reinterpret_cast<const unsigned char*>(s);

  while (*p) {
    uint32_t codepoint = decodeUtf8Codepoint(p);
    drawChar(codepoint);
    ++written;
  }

  return written;
}

size_t TextRenderer::print(const String& s) {
  return print(s.c_str());
}

void TextRenderer::getTextBounds(const char* str, int16_t x, int16_t y, int16_t* x1, int16_t* y1, uint16_t* w,
                                 uint16_t* h) {
  if (!str) {
    if (x1)
      *x1 = x;
    if (y1)
      *y1 = y;
    if (w)
      *w = 0;
    if (h)
      *h = 0;
    return;
  }

  int16_t minx = x;
  int16_t miny = y;
  uint16_t width = 0;
  uint16_t height = 0;

  if (currentFont) {
    const SimpleGFXfont* f = currentFont;
    uint16_t totalWidth = 0;
    const unsigned char* p = reinterpret_cast<const unsigned char*>(str);

    while (*p) {
      uint32_t codepoint = decodeUtf8Codepoint(p);
      int glyphIndex = findGlyphIndex(f, codepoint);

      if (glyphIndex >= 0) {
        const SimpleGFXglyph* glyph = &f->glyph[glyphIndex];
        totalWidth += glyph->xAdvance + GLYPH_PADDING;
      } else {
        totalWidth += FALLBACK_GLYPH_WIDTH;
      }
    }

    width = totalWidth;
    height = (f->yAdvance > 0) ? f->yAdvance : 10;
  }

  if (x1)
    *x1 = minx;
  if (y1)
    *y1 = miny;
  if (w)
    *w = width;
  if (h)
    *h = height;
}

void TextRenderer::drawChar(uint32_t codepoint) {
  if (!currentFont) {
    return;
  }

  const SimpleGFXfont* f = currentFont;
  int glyphIndex = findGlyphIndex(f, codepoint);

  if (glyphIndex < 0) {
    // Unsupported codepoint; advance by fallback amount
    cursorX += FALLBACK_GLYPH_WIDTH;
    return;
  }

  const SimpleGFXglyph* glyph = &f->glyph[glyphIndex];
  const uint8_t* bitmap = f->bitmap;
  const uint8_t* bitmap_gray_lsb = f->bitmap_gray_lsb;
  const uint8_t* bitmap_gray_msb = f->bitmap_gray_msb;
  uint16_t bitmapOffset = glyph->bitmapOffset;
  uint8_t w = glyph->width;
  uint8_t h = glyph->height;
  int8_t xOffset = glyph->xOffset;
  int8_t yOffset = glyph->yOffset;

  // Calculate row stride in bytes (width rounded up to byte boundary)
  uint8_t rowStride = (w + 7) / 8;

  // Render each pixel in the glyph
  for (uint8_t yy = 0; yy < h; yy++) {
    for (uint8_t xx = 0; xx < w; xx++) {
      int16_t px = cursorX + xOffset + xx;
      int16_t py = cursorY + yOffset + yy;

      // Calculate bitmap byte and bit positions for current pixel
      uint16_t byteIndex = bitmapOffset + (yy * rowStride) + (xx / 8);
      uint8_t bitMask = 1 << (7 - (xx % 8));

      if (grayscaleMode && bitmap_gray_lsb && bitmap_gray_msb) {
        // Grayscale mode - read from all three buffers
        bool bwBit = (bitmap[byteIndex] & bitMask) != 0;
        bool lsbBit = (bitmap_gray_lsb[byteIndex] & bitMask) != 0;
        bool msbBit = (bitmap_gray_msb[byteIndex] & bitMask) != 0;

        drawPixelGray(px, py, !bwBit, !lsbBit, !msbBit);
      } else {
        // Black & white mode - only use main bitmap
        bool pixelOn = (bitmap[byteIndex] & bitMask) == 0;
        if (pixelOn) {
          drawPixel(px, py, textColor);
        }
      }
    }
  }

  if (grayscaleMode && bitmap_gray_lsb && bitmap_gray_msb) {
    // In grayscale mode, ensure we inform the display to use grayscale drawing
    display.enableGrayscaleDrawing(true);
  }

  // Advance cursor by xAdvance
  cursorX += glyph->xAdvance + GLYPH_PADDING;
}
