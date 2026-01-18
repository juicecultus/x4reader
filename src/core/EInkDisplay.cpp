#include "EInkDisplay.h"

#ifdef USE_M5UNIFIED
#include <algorithm>
#include <FastEPD.h>
#include <esp_heap_caps.h>

static FASTEPD g_epd;
static bool g_epdInited = false;
static bool g_epdHasBaseline = false;
static bool g_epdLoggedFirstFlush = false;
#endif

#include <cstring>
#include <fstream>
#include <vector>

#if defined(ARDUINO) && !defined(USE_M5UNIFIED)
#include <bb_epaper.h>
#endif

// SSD1677 command definitions
// Initialization and reset
#define CMD_SOFT_RESET 0x12             // Soft reset
#define CMD_BOOSTER_SOFT_START 0x0C     // Booster soft-start control
#define CMD_DRIVER_OUTPUT_CONTROL 0x01  // Driver output control
#define CMD_BORDER_WAVEFORM 0x3C        // Border waveform control
#define CMD_TEMP_SENSOR_CONTROL 0x18    // Temperature sensor control

// RAM and buffer management
#define CMD_DATA_ENTRY_MODE 0x11     // Data entry mode
#define CMD_SET_RAM_X_RANGE 0x44     // Set RAM X address range
#define CMD_SET_RAM_Y_RANGE 0x45     // Set RAM Y address range
#define CMD_SET_RAM_X_COUNTER 0x4E   // Set RAM X address counter
#define CMD_SET_RAM_Y_COUNTER 0x4F   // Set RAM Y address counter
#define CMD_WRITE_RAM_BW 0x24        // Write to BW RAM (current frame)
#define CMD_WRITE_RAM_RED 0x26       // Write to RED RAM (used for fast refresh)
#define CMD_AUTO_WRITE_BW_RAM 0x46   // Auto write BW RAM
#define CMD_AUTO_WRITE_RED_RAM 0x47  // Auto write RED RAM

// Display update and refresh
#define CMD_DISPLAY_UPDATE_CTRL1 0x21  // Display update control 1
#define CMD_DISPLAY_UPDATE_CTRL2 0x22  // Display update control 2
#define CMD_MASTER_ACTIVATION 0x20     // Master activation
#define CTRL1_NORMAL 0x00              // Normal mode - compare RED vs BW for partial
#define CTRL1_BYPASS_RED 0x40          // Bypass RED RAM (treat as 0) - for full refresh

// LUT and voltage settings
#define CMD_WRITE_LUT 0x32       // Write LUT
#define CMD_GATE_VOLTAGE 0x03    // Gate voltage
#define CMD_SOURCE_VOLTAGE 0x04  // Source voltage
#define CMD_WRITE_VCOM 0x2C      // Write VCOM
#define CMD_WRITE_TEMP 0x1A      // Write temperature

// Power management
#define CMD_DEEP_SLEEP 0x10  // Deep sleep

// Custom LUT for fast refresh
const unsigned char lut_grayscale[] PROGMEM = {
    // 00 black/white
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // 01 light gray
    0x54, 0x54, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // 10 gray
    0xAA, 0xA0, 0xA8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // 11 dark gray
    0xA2, 0x22, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // L4 (VCOM)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

    // TP/RP groups (global timing)
    0x01, 0x01, 0x01, 0x01, 0x00,  // G0: A=1 B=1 C=1 D=1 RP=0 (4 frames)
    0x01, 0x01, 0x01, 0x01, 0x00,  // G1: A=1 B=1 C=1 D=1 RP=0 (4 frames)
    0x01, 0x01, 0x01, 0x01, 0x00,  // G2: A=0 B=0 C=0 D=0 RP=0 (4 frames)
    0x00, 0x00, 0x00, 0x00, 0x00,  // G3: A=0 B=0 C=0 D=0 RP=0
    0x00, 0x00, 0x00, 0x00, 0x00,  // G4: A=0 B=0 C=0 D=0 RP=0
    0x00, 0x00, 0x00, 0x00, 0x00,  // G5: A=0 B=0 C=0 D=0 RP=0
    0x00, 0x00, 0x00, 0x00, 0x00,  // G6: A=0 B=0 C=0 D=0 RP=0
    0x00, 0x00, 0x00, 0x00, 0x00,  // G7: A=0 B=0 C=0 D=0 RP=0
    0x00, 0x00, 0x00, 0x00, 0x00,  // G8: A=0 B=0 C=0 D=0 RP=0
    0x00, 0x00, 0x00, 0x00, 0x00,  // G9: A=0 B=0 C=0 D=0 RP=0

    // Frame rate
    0x8F, 0x8F, 0x8F, 0x8F, 0x8F,

    // Voltages (VGH, VSH1, VSH2, VSL, VCOM)
    0x17, 0x41, 0xA8, 0x32, 0x30,

    // Reserved
    0x00, 0x00};

const unsigned char lut_grayscale_revert[] PROGMEM = {
    // 00 black/white
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // 10 gray
    0x54, 0x54, 0x54, 0x54, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // 01 light gray
    0xA8, 0xA8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // 11 dark gray
    0xFC, 0xFC, 0xFC, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // L4 (VCOM)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

    // TP/RP groups (global timing)
    0x01, 0x01, 0x01, 0x01, 0x01,  // G0: A=1 B=1 C=1 D=1 RP=0 (4 frames)
    0x01, 0x01, 0x01, 0x01, 0x01,  // G1: A=1 B=1 C=1 D=1 RP=0 (4 frames)
    0x01, 0x01, 0x01, 0x01, 0x00,  // G2: A=0 B=0 C=0 D=0 RP=0 (4 frames)
    0x01, 0x01, 0x01, 0x01, 0x00,  // G3: A=0 B=0 C=0 D=0 RP=0
    0x00, 0x00, 0x00, 0x00, 0x00,  // G4: A=0 B=0 C=0 D=0 RP=0
    0x00, 0x00, 0x00, 0x00, 0x00,  // G5: A=0 B=0 C=0 D=0 RP=0
    0x00, 0x00, 0x00, 0x00, 0x00,  // G6: A=0 B=0 C=0 D=0 RP=0
    0x00, 0x00, 0x00, 0x00, 0x00,  // G7: A=0 B=0 C=0 D=0 RP=0
    0x00, 0x00, 0x00, 0x00, 0x00,  // G8: A=0 B=0 C=0 D=0 RP=0
    0x00, 0x00, 0x00, 0x00, 0x00,  // G9: A=0 B=0 C=0 D=0 RP=0

    // Frame rate
    0x8F, 0x8F, 0x8F, 0x8F, 0x8F,

    // Voltages (VGH, VSH1, VSH2, VSL, VCOM)
    0x17, 0x41, 0xA8, 0x32, 0x30,

    // Reserved
    0x00, 0x00};

EInkDisplay::EInkDisplay(int8_t sclk, int8_t mosi, int8_t cs, int8_t dc, int8_t rst, int8_t busy)
    : _sclk(sclk),
      _mosi(mosi),
      _cs(cs),
      _dc(dc),
      _rst(rst),
      _busy(busy),
#ifdef USE_M5UNIFIED
      frameBuffer0(nullptr),
      frameBuffer1(nullptr),
#endif
      frameBuffer(nullptr),
      frameBufferActive(nullptr),
      customLutActive(false),
      isScreenOn(false),
      inGrayscaleMode(false),
      drawGrayscale(false) {
  Serial.printf("[%lu] EInkDisplay: Constructor called\n", millis());
  Serial.printf("[%lu]   SCLK=%d, MOSI=%d, CS=%d, DC=%d, RST=%d, BUSY=%d\n", millis(), sclk, mosi, cs, dc, rst, busy);
}

EInkDisplay::~EInkDisplay() {
#if defined(ARDUINO) && !defined(USE_M5UNIFIED)
  if (bbep) {
    delete bbep;
    bbep = nullptr;
  }
#endif
}

void EInkDisplay::begin() {
  Serial.printf("[%lu] EInkDisplay: begin() called\n", millis());

#ifdef USE_M5UNIFIED
  // Paper S3: allocate framebuffers from PSRAM to free internal RAM for stack
  if (!frameBuffer0) {
    frameBuffer0 = (uint8_t*)heap_caps_malloc(BUFFER_SIZE, MALLOC_CAP_SPIRAM);
    if (!frameBuffer0) {
      Serial.println("ERROR: Failed to allocate frameBuffer0 from PSRAM!");
    }
  }
  if (!frameBuffer1) {
    frameBuffer1 = (uint8_t*)heap_caps_malloc(BUFFER_SIZE, MALLOC_CAP_SPIRAM);
    if (!frameBuffer1) {
      Serial.println("ERROR: Failed to allocate frameBuffer1 from PSRAM!");
    }
  }
  Serial.printf("[%lu]   PSRAM frame buffers (2 x %lu bytes = %luKB)\n", millis(), BUFFER_SIZE, (BUFFER_SIZE * 2) / 1024);
#else
  Serial.printf("[%lu]   Static frame buffers (2 x %lu bytes)\n", millis(), BUFFER_SIZE);
#endif

  frameBuffer = frameBuffer0;
  frameBufferActive = frameBuffer1;

  // Initialize to white
  memset(frameBuffer0, 0xFF, BUFFER_SIZE);
  memset(frameBuffer1, 0xFF, BUFFER_SIZE);
  Serial.printf("[%lu]   Initializing e-ink display driver...\n", millis());

#ifdef USE_M5UNIFIED
  // Paper S3: use FastEPD's native PaperS3 panel driver.
  if (!g_epdInited) {
    const int rc = g_epd.initPanel(BB_PANEL_M5PAPERS3);
    Serial.printf("[%lu] FastEPD initPanel returned %d\n", millis(), rc);
    g_epd.setMode(BB_MODE_1BPP);

    // Increase partial update passes for cleaner text rendering (reduces ghosting)
    // Default is typically 2-3 passes; more passes = cleaner but slower
    g_epd.setPasses(12, 12);  // 10 partial passes, 10 full passes

    // Keep FastEPD at its native rotation.
    // FastEPD's internal 1bpp packing differs across rotations; directly memcpy'ing
    // into a rotated buffer can produce corrupted vertical artifacts.
    // We instead rotate/blit Macroreader's portrait framebuffer into FastEPD's native
    // buffer format in displayBuffer().
    g_epd.setRotation(0);
    Serial.printf("[%lu] FastEPD rotated size: %d x %d\n", millis(), (int)g_epd.width(), (int)g_epd.height());
    g_epd.clearWhite(true);
    g_epd.backupPlane();
    // Ensure the panel has a known on-screen baseline before any partial updates.
    g_epd.fullUpdate(CLEAR_SLOW, true, NULL);
    g_epd.backupPlane();
    g_epdInited = true;
    g_epdHasBaseline = true;
  }
  isScreenOn = true;

#elif defined(ARDUINO) && !defined(USE_M5UNIFIED)
  // Initialize SPI with custom pins
  SPI.begin(_sclk, -1, _mosi, _cs);
  spiSettings = SPISettings(40000000, MSBFIRST, SPI_MODE0);  // MODE0 is standard for SSD1677
  Serial.printf("[%lu]   SPI initialized at 40 MHz, Mode 0\n", millis());

  // bb_epaper handles SPI init internally once we provide GPIO/pin mapping.
  if (bbep) {
    delete bbep;
    bbep = nullptr;
  }

  // Setup GPIO pins
  pinMode(_cs, OUTPUT);
  pinMode(_dc, OUTPUT);
  pinMode(_rst, OUTPUT);
  pinMode(_busy, INPUT);

  digitalWrite(_cs, HIGH);
  digitalWrite(_dc, HIGH);

  Serial.printf("[%lu]   GPIO pins configured\n", millis());

  // Reset display
  resetDisplay();

  // Initialize display controller
  initDisplayController();

  // The controller is initialized, but we haven't necessarily turned on the
  // analog/clock rails yet.
  isScreenOn = false;

  Serial.printf("[%lu]   E-ink display driver initialized\n", millis());
#endif
}

bool EInkDisplay::supportsGrayscale() const {
#ifdef USE_M5UNIFIED
  // Paper S3: FastEPD handles display differently; skip legacy grayscale path
  return false;
#else
  return true;
#endif
}

// ============================================================================
// Low-level display control methods
// ============================================================================

void EInkDisplay::resetDisplay() {
  Serial.printf("[%lu]   Resetting display...\n", millis());
  digitalWrite(_rst, HIGH);
  delay(20);
  digitalWrite(_rst, LOW);
  delay(2);
  digitalWrite(_rst, HIGH);
  delay(20);
  Serial.printf("[%lu]   Display reset complete\n", millis());
}

void EInkDisplay::sendCommand(uint8_t command) {
  SPI.beginTransaction(spiSettings);
  digitalWrite(_dc, LOW);  // Command mode
  digitalWrite(_cs, LOW);  // Select chip
  SPI.transfer(command);
  digitalWrite(_cs, HIGH);  // Deselect chip
  SPI.endTransaction();
}

void EInkDisplay::sendData(uint8_t data) {
  SPI.beginTransaction(spiSettings);
  digitalWrite(_dc, HIGH);  // Data mode
  digitalWrite(_cs, LOW);   // Select chip
  SPI.transfer(data);
  digitalWrite(_cs, HIGH);  // Deselect chip
  SPI.endTransaction();
}

void EInkDisplay::sendData(const uint8_t* data, uint16_t length) {
  SPI.beginTransaction(spiSettings);
  digitalWrite(_dc, HIGH);       // Data mode
  digitalWrite(_cs, LOW);        // Select chip
  SPI.writeBytes(data, length);  // Transfer all bytes
  digitalWrite(_cs, HIGH);       // Deselect chip
  SPI.endTransaction();
}

void EInkDisplay::waitWhileBusy(const char* comment) {
  unsigned long start = millis();
  while (digitalRead(_busy) == HIGH) {
    delay(1);
    if (millis() - start > 10000) {
      Serial.printf("[%lu]   Timeout waiting for busy%s\n", millis(), comment ? comment : "");
      break;
    }
  }
  if (comment) {
    Serial.printf("[%lu]   Wait complete: %s (%lu ms)\n", millis(), comment, millis() - start);
  }
}

void EInkDisplay::initDisplayController() {
  Serial.printf("[%lu]   Initializing SSD1677 controller...\n", millis());

  const uint8_t TEMP_SENSOR_INTERNAL = 0x80;

  // Soft reset
  sendCommand(CMD_SOFT_RESET);
  waitWhileBusy(" CMD_SOFT_RESET");

  // Temperature sensor control (internal)
  sendCommand(CMD_TEMP_SENSOR_CONTROL);
  sendData(TEMP_SENSOR_INTERNAL);

  // Booster soft-start control (GDEQ0426T82 specific values)
  sendCommand(CMD_BOOSTER_SOFT_START);
  sendData(0xAE);
  sendData(0xC7);
  sendData(0xC3);
  sendData(0xC0);
  sendData(0x40);

  // Driver output control: set display height (480) and scan direction
  const uint16_t HEIGHT = 480;
  sendCommand(CMD_DRIVER_OUTPUT_CONTROL);
  sendData((HEIGHT - 1) % 256);  // gates A0..A7 (low byte)
  sendData((HEIGHT - 1) / 256);  // gates A8..A9 (high byte)
  sendData(0x02);                // SM=1 (interlaced), TB=0

  // Border waveform control
  sendCommand(CMD_BORDER_WAVEFORM);
  sendData(0x01);

  // Set up full screen RAM area
  setRamArea(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);

  Serial.printf("[%lu]   Clearing RAM buffers...\n", millis());
  sendCommand(CMD_AUTO_WRITE_BW_RAM);  // Auto write BW RAM
  sendData(0xF7);
  waitWhileBusy(" CMD_AUTO_WRITE_BW_RAM");

  sendCommand(CMD_AUTO_WRITE_RED_RAM);  // Auto write RED RAM
  sendData(0xF7);                       // Fill with white pattern
  waitWhileBusy(" CMD_AUTO_WRITE_RED_RAM");

  Serial.printf("[%lu]   SSD1677 controller initialized\n", millis());
}

void EInkDisplay::setRamArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
#if defined(ARDUINO) && !defined(USE_M5UNIFIED)
  const uint16_t WIDTH = 800;
  const uint16_t HEIGHT = 480;
  const uint8_t DATA_ENTRY_X_INC_Y_DEC = 0x01;

  // Reverse Y coordinate (gates are reversed on this display)
  y = HEIGHT - y - h;

  // Set data entry mode (X increment, Y decrement for reversed gates)
  sendCommand(CMD_DATA_ENTRY_MODE);
  sendData(DATA_ENTRY_X_INC_Y_DEC);

  // Set RAM X address range (start, end) - X is in PIXELS
  sendCommand(CMD_SET_RAM_X_RANGE);
  sendData(x % 256);            // start low byte
  sendData(x / 256);            // start high byte
  sendData((x + w - 1) % 256);  // end low byte
  sendData((x + w - 1) / 256);  // end high byte

  // Set RAM Y address range (start, end) - Y is in PIXELS
  sendCommand(CMD_SET_RAM_Y_RANGE);
  sendData((y + h - 1) % 256);  // start low byte
  sendData((y + h - 1) / 256);  // start high byte
  sendData(y % 256);            // end low byte
  sendData(y / 256);            // end high byte

  // Set RAM X address counter - X is in PIXELS
  sendCommand(CMD_SET_RAM_X_COUNTER);
  sendData(x % 256);  // low byte
  sendData(x / 256);  // high byte

  // Set RAM Y address counter - Y is in PIXELS
  sendCommand(CMD_SET_RAM_Y_COUNTER);
  sendData((y + h - 1) % 256);  // low byte
  sendData((y + h - 1) / 256);  // high byte
#else
  (void)x;
  (void)y;
  (void)w;
  (void)h;
#endif
}

void EInkDisplay::clearScreen(uint8_t color) {
  memset(frameBuffer, color, BUFFER_SIZE);
}

void EInkDisplay::drawImage(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                            bool fromProgmem) {
  if (!frameBuffer) {
    Serial.printf("[%lu]   ERROR: Frame buffer not allocated!\n", millis());
    return;
  }

  // Calculate bytes per line for the image
  uint16_t imageWidthBytes = (w + 7) / 8;

  // Copy image data to frame buffer
  for (uint16_t row = 0; row < h; row++) {
    uint16_t destY = y + row;
    if (destY >= DISPLAY_HEIGHT)
      break;

    uint16_t destOffset = destY * DISPLAY_WIDTH_BYTES + (x / 8);
    uint16_t srcOffset = row * imageWidthBytes;

    for (uint16_t col = 0; col < imageWidthBytes; col++) {
      if ((x / 8 + col) >= DISPLAY_WIDTH_BYTES)
        break;

      if (fromProgmem) {
        frameBuffer[destOffset + col] = pgm_read_byte(&imageData[srcOffset + col]);
      } else {
        frameBuffer[destOffset + col] = imageData[srcOffset + col];
      }
    }
  }

  Serial.printf("[%lu]   Image drawn to frame buffer\n", millis());
}

void EInkDisplay::writeRamBuffer(uint8_t ramBuffer, const uint8_t* data, uint32_t size) {
#if defined(ARDUINO) && !defined(USE_M5UNIFIED)
  const char* bufferName = (ramBuffer == CMD_WRITE_RAM_BW) ? "BW" : "RED";
  unsigned long startTime = millis();
  Serial.printf("[%lu]   Writing frame buffer to %s RAM (%lu bytes)...\n", startTime, bufferName, size);

  sendCommand(ramBuffer);
  sendData(data, size);

  unsigned long duration = millis() - startTime;
  Serial.printf("[%lu]   %s RAM write complete (%lu ms)\n", millis(), bufferName, duration);
#else
  (void)ramBuffer;
  (void)data;
  (void)size;
#endif
}

void EInkDisplay::setFramebuffer(const uint8_t* bwBuffer) {
  memcpy(frameBuffer, bwBuffer, BUFFER_SIZE);
}

void EInkDisplay::swapBuffers() {
  uint8_t* temp = frameBuffer;
  frameBuffer = frameBufferActive;
  frameBufferActive = temp;
}

void EInkDisplay::grayscaleRevert() {
#if defined(ARDUINO) && !defined(USE_M5UNIFIED)
  if (!inGrayscaleMode) {
    return;
  }

  inGrayscaleMode = false;

  // Load the revert LUT
  setCustomLUT(true, lut_grayscale_revert);
  refreshDisplay(FAST_REFRESH);
  setCustomLUT(false);
#else
  inGrayscaleMode = false;
#endif
}

void EInkDisplay::copyGrayscaleLsbBuffers(const uint8_t* lsbBuffer) {
#if defined(ARDUINO) && !defined(USE_M5UNIFIED)
  setRamArea(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
  writeRamBuffer(CMD_WRITE_RAM_BW, lsbBuffer, BUFFER_SIZE);
#else
  (void)lsbBuffer;
#endif
}

void EInkDisplay::copyGrayscaleMsbBuffers(const uint8_t* msbBuffer) {
#if defined(ARDUINO) && !defined(USE_M5UNIFIED)
  setRamArea(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
  writeRamBuffer(CMD_WRITE_RAM_RED, msbBuffer, BUFFER_SIZE);
#else
  (void)msbBuffer;
#endif
}

void EInkDisplay::copyGrayscaleBuffers(const uint8_t* lsbBuffer, const uint8_t* msbBuffer) {
#if defined(ARDUINO) && !defined(USE_M5UNIFIED)
  setRamArea(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
  writeRamBuffer(CMD_WRITE_RAM_BW, lsbBuffer, BUFFER_SIZE);
  writeRamBuffer(CMD_WRITE_RAM_RED, msbBuffer, BUFFER_SIZE);
#else
  (void)lsbBuffer;
  (void)msbBuffer;
#endif
}

void EInkDisplay::displayBuffer(RefreshMode mode) {
#ifdef USE_M5UNIFIED
  if (!g_epdInited) {
    (void)mode;
    return;
  }

  const uint16_t srcW = DISPLAY_WIDTH;
  const uint16_t srcH = DISPLAY_HEIGHT;
  const uint16_t dstW = (uint16_t)g_epd.width();
  const uint16_t dstH = (uint16_t)g_epd.height();

  const uint8_t* src = frameBuffer;
  const uint32_t srcRowBytes = DISPLAY_WIDTH_BYTES;

  uint8_t* dst = g_epd.currentBuffer();
  const uint32_t dstRowBytes = (dstW + 7) / 8;

  if (!g_epdLoggedFirstFlush) {
    Serial.printf("[%lu] FastEPD flush: src=%ux%u dst=%ux%u srcRB=%u dstRB=%u\n", millis(), (unsigned)srcW,
                  (unsigned)srcH, (unsigned)dstW, (unsigned)dstH, (unsigned)srcRowBytes, (unsigned)dstRowBytes);
    g_epdLoggedFirstFlush = true;
  }

  // Rotate portrait -> FastEPD's native buffer format.
  // This avoids rotation-specific 1bpp bit packing differences inside FastEPD.
  memset(dst, 0xFF, dstRowBytes * dstH);

  for (uint16_t yOut = 0; yOut < dstH; yOut++) {
    uint8_t* dstRow = dst + (uint32_t)yOut * dstRowBytes;
    for (uint16_t xOut = 0; xOut < dstW; xOut++) {
      uint16_t xIn = 0;
      uint16_t yIn = 0;

#ifdef M5_PORTRAIT_ROTATION
      if (M5_PORTRAIT_ROTATION == 1) {
        // Rotate portrait -> landscape (CW 90)
        xIn = yOut;
        yIn = (uint16_t)(srcH - 1 - xOut);
      } else {
        // Rotate portrait -> landscape (CCW 90)
        xIn = (uint16_t)(srcW - 1 - yOut);
        yIn = xOut;
      }
#else
      // Default to CCW 90
      xIn = (uint16_t)(srcW - 1 - yOut);
      yIn = xOut;
#endif

      if (xIn >= srcW || yIn >= srcH) {
        continue;
      }

      const uint8_t* srcRow = src + (uint32_t)yIn * srcRowBytes;
      const uint8_t b = srcRow[xIn / 8];
      const uint8_t bit = 7 - (xIn % 8);
      const bool isWhite = ((b >> bit) & 0x01) != 0;
      const uint8_t dstMask = (uint8_t)(0x80 >> (xOut & 7));
      if (isWhite) {
        dstRow[xOut / 8] |= dstMask;
      } else {
        dstRow[xOut / 8] &= (uint8_t)~dstMask;
      }
    }
  }

  if (mode == FULL_REFRESH) {
    g_epd.fullUpdate(CLEAR_SLOW, true, NULL);
    g_epd.backupPlane();
    g_epdHasBaseline = true;
  } else if (!g_epdHasBaseline) {
    g_epd.fullUpdate(CLEAR_SLOW, true, NULL);
    g_epd.backupPlane();
    g_epdHasBaseline = true;
  } else {
    // Fast refresh paths: prefer partial updates to reduce flashing.
    g_epd.partialUpdate(true, 0, 4095);
    g_epd.backupPlane();
  }

  swapBuffers();

#elif defined(ARDUINO) && !defined(USE_M5UNIFIED)
  if (!isScreenOn) {
    // Force half refresh if screen is off
    mode = HALF_REFRESH;
  }

  // If currently in grayscale mode, revert first to black/white
  if (inGrayscaleMode) {
    inGrayscaleMode = false;
    grayscaleRevert();
  }

  // Set up full screen RAM area
  setRamArea(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);

  if (mode != FAST_REFRESH) {
    // For full refresh, write to both buffers before refresh
    writeRamBuffer(CMD_WRITE_RAM_BW, frameBuffer, BUFFER_SIZE);
    writeRamBuffer(CMD_WRITE_RAM_RED, frameBuffer, BUFFER_SIZE);
  } else {
    // For fast refresh, write to BW buffer only
    writeRamBuffer(CMD_WRITE_RAM_BW, frameBuffer, BUFFER_SIZE);
    // In dual buffer mode, we write back frameBufferActive which is the last frame
    writeRamBuffer(CMD_WRITE_RAM_RED, frameBufferActive, BUFFER_SIZE);
  }

  swapBuffers();

  // Refresh the display
  refreshDisplay(mode);
#else
  (void)mode;
#endif
}

void EInkDisplay::displayGrayBuffer(bool turnOffScreen) {
#ifdef USE_M5UNIFIED
  // Paper S3: FastEPD handles grayscale internally; no custom LUT needed.
  drawGrayscale = false;
  inGrayscaleMode = true;
  (void)turnOffScreen;
#elif defined(ARDUINO) && !defined(USE_M5UNIFIED)
  drawGrayscale = false;
  inGrayscaleMode = true;

  // activate the custom LUT for grayscale rendering and refresh
  setCustomLUT(true, lut_grayscale);
  refreshDisplay(FAST_REFRESH, turnOffScreen);
  setCustomLUT(false);
#else
  (void)turnOffScreen;
#endif
}

void EInkDisplay::refreshDisplay(RefreshMode mode, bool turnOffScreen) {
#ifdef USE_M5UNIFIED
  // Paper S3: displayBuffer() already pushes a full frame.
  // We keep this as a no-op for now.
  (void)mode;
  (void)turnOffScreen;

#elif defined(ARDUINO) && !defined(USE_M5UNIFIED)
  // Configure Display Update Control 1
  sendCommand(CMD_DISPLAY_UPDATE_CTRL1);
  sendData((mode == FAST_REFRESH) ? CTRL1_NORMAL : CTRL1_BYPASS_RED);  // Configure buffer comparison mode

  // Select appropriate display mode based on refresh type
  uint8_t displayMode = 0x00;

  // Always enable CLOCK_ON + ANALOG_ON for any refresh.
  // Without these, the panel can refresh extremely faint/invisible.
  displayMode |= 0xC0;

  // Turn off screen if requested
  if (turnOffScreen) {
    isScreenOn = false;
    displayMode |= 0x03;  // Set ANALOG_OFF_PHASE and CLOCK_OFF bits
  } else {
    isScreenOn = true;
  }

  if (mode == FULL_REFRESH) {
    // TEMP_LOAD | MODE_SELECT | DISPLAY_START
    displayMode |= 0x34;
  } else if (mode == HALF_REFRESH) {
    // Write high temp to the register for a faster refresh
    sendCommand(CMD_WRITE_TEMP);
    sendData(0x5A);
    displayMode |= 0xD4;
  } else {  // FAST_REFRESH
    displayMode |= customLutActive ? 0x0C : 0x1C;
  }

  // Power on and refresh display
  const char* refreshType = (mode == FULL_REFRESH) ? "full" : (mode == HALF_REFRESH) ? "half" : "fast";
  Serial.printf("[%lu]   Powering on display 0x%02X (%s refresh)...\n", millis(), displayMode, refreshType);
  sendCommand(CMD_DISPLAY_UPDATE_CTRL2);
  sendData(displayMode);

  sendCommand(CMD_MASTER_ACTIVATION);

  // Wait for display to finish updating
  Serial.printf("[%lu]   Waiting for display refresh...\n", millis());
  waitWhileBusy(refreshType);
#else
  (void)mode;
  (void)turnOffScreen;
#endif
}

void EInkDisplay::setCustomLUT(bool enabled, const unsigned char* lutData) {
#if defined(ARDUINO) && !defined(USE_M5UNIFIED)
  if (enabled) {
    Serial.printf("[%lu]   Loading custom LUT...\n", millis());

    // Load custom LUT (first 105 bytes: VS + TP/RP + frame rate)
    sendCommand(CMD_WRITE_LUT);
    for (uint16_t i = 0; i < 105; i++) {
      sendData(pgm_read_byte(&lutData[i]));
    }

    // Set voltage values from bytes 105-109
    sendCommand(CMD_GATE_VOLTAGE);  // VGH
    sendData(pgm_read_byte(&lutData[105]));

    sendCommand(CMD_SOURCE_VOLTAGE);         // VSH1, VSH2, VSL
    sendData(pgm_read_byte(&lutData[106]));  // VSH1
    sendData(pgm_read_byte(&lutData[107]));  // VSH2
    sendData(pgm_read_byte(&lutData[108]));  // VSL

    sendCommand(CMD_WRITE_VCOM);  // VCOM
    sendData(pgm_read_byte(&lutData[109]));

    customLutActive = true;
    Serial.printf("[%lu]   Custom LUT loaded\n", millis());
  } else {
    customLutActive = false;
    Serial.printf("[%lu]   Custom LUT disabled\n", millis());
  }
#else
  (void)enabled;
  (void)lutData;
  customLutActive = false;
#endif
}

void EInkDisplay::deepSleep() {
#ifdef USE_M5UNIFIED
  // M5Unified owns display power management. No-op for now.
  isScreenOn = false;

#elif defined(ARDUINO) && !defined(USE_M5UNIFIED)
  Serial.printf("[%lu]   Preparing display for deep sleep...\n", millis());

  // First, power down the display properly
  // This shuts down the analog power rails and clock
  if (isScreenOn) {
    sendCommand(CMD_DISPLAY_UPDATE_CTRL1);
    sendData(CTRL1_BYPASS_RED);

    sendCommand(CMD_DISPLAY_UPDATE_CTRL2);
    sendData(0x03);  // Set ANALOG_OFF_PHASE (bit 1) and CLOCK_OFF (bit 0)

    sendCommand(CMD_MASTER_ACTIVATION);

    // Wait for the power-down sequence to complete
    waitWhileBusy(" display power-down");
    isScreenOn = false;
  }

  // Now enter deep sleep mode
  Serial.printf("[%lu]   Entering deep sleep mode...\n", millis());
  sendCommand(CMD_DEEP_SLEEP);
  sendData(0x01);  // Enter deep sleep
#endif
}

void EInkDisplay::saveFrameBufferAsPBM(const char* filename) {
#ifndef ARDUINO
  const uint8_t* buffer = getFrameBuffer();

  std::ofstream file(filename, std::ios::binary);
  if (!file) {
    Serial.printf("Failed to open %s for writing\n", filename);
    return;
  }

  // Rotate the image 90 degrees counterclockwise when saving
  // Original buffer: 800x480 (landscape)
  // Output image: 480x800 (portrait)
  const int DISPLAY_WIDTH_LOCAL = DISPLAY_WIDTH;    // 800
  const int DISPLAY_HEIGHT_LOCAL = DISPLAY_HEIGHT;  // 480
  const int DISPLAY_WIDTH_BYTES_LOCAL = DISPLAY_WIDTH_LOCAL / 8;

  file << "P4\n";  // Binary PBM
  file << DISPLAY_HEIGHT_LOCAL << " " << DISPLAY_WIDTH_LOCAL << "\n";

  // Create rotated buffer
  std::vector<uint8_t> rotatedBuffer((DISPLAY_HEIGHT_LOCAL / 8) * DISPLAY_WIDTH_LOCAL, 0);

  for (int outY = 0; outY < DISPLAY_WIDTH_LOCAL; outY++) {
    for (int outX = 0; outX < DISPLAY_HEIGHT_LOCAL; outX++) {
      int inX = outY;
      int inY = DISPLAY_HEIGHT_LOCAL - 1 - outX;

      int inByteIndex = inY * DISPLAY_WIDTH_BYTES_LOCAL + (inX / 8);
      int inBitPosition = 7 - (inX % 8);
      bool isWhite = (buffer[inByteIndex] >> inBitPosition) & 1;

      int outByteIndex = outY * (DISPLAY_HEIGHT_LOCAL / 8) + (outX / 8);
      int outBitPosition = 7 - (outX % 8);
      if (!isWhite) {  // Invert: e-ink white=1 -> PBM black=1
        rotatedBuffer[outByteIndex] |= (1 << outBitPosition);
      }
    }
  }

  file.write(reinterpret_cast<const char*>(rotatedBuffer.data()), rotatedBuffer.size());
  file.close();
  Serial.printf("Saved framebuffer to %s\n", filename);
#else
  (void)filename;
  Serial.println("saveFrameBufferAsPBM is not supported on Arduino builds.");
#endif
}
