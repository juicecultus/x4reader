#include "UIManager.h"

#include <Arduino.h>
#include <resources/fonts/FontManager.h>

#include <WiFi.h>

#include "../content/epub/epub_parser.h"
#include <WiFiUdp.h>
#include <time.h>
#include <sys/time.h>

#include <esp_sleep.h>
#include <esp_heap_caps.h>

#include <esp_system.h>

#include "core/ImageDecoder.h"
#include "core/Settings.h"
#include "core/BatteryMonitor.h"
#include "resources/images/bebop_image.h"
#include "ui/screens/FileBrowserScreen.h"
#include "ui/screens/ImageViewerScreen.h"
#include "ui/screens/ChaptersScreen.h"
#include "ui/screens/SettingsScreen.h"
#include "ui/screens/TextViewerScreen.h"
#include "ui/screens/XtcViewerScreen.h"
#include "ui/screens/ClockSettingsScreen.h"
#include "ui/screens/TimezoneSelectScreen.h"
#include "ui/screens/WifiPasswordEntryScreen.h"
#include "ui/screens/WifiSettingsScreen.h"
#include "ui/screens/WifiSsidSelectScreen.h"

#include <resources/fonts/other/MenuFontSmall.h>

#include "content/epub/EpubReader.h"

RTC_DATA_ATTR static int32_t g_lastSleepCoverIndex = -1;
RTC_DATA_ATTR static int64_t g_lastGoodEpochSec = 0;

static uint32_t fnv1a32_ui(const char* s) {
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

static bool writeRawBufferToFile_ui(const char* path, const uint8_t* buf, size_t len) {
  if (!path || !buf || len == 0) {
    return false;
  }
  if (SD.exists(path)) {
    SD.remove(path);
  }
  File f = SD.open(path, FILE_WRITE);
  if (!f) {
    return false;
  }
  const size_t n = f.write(buf, len);
  f.close();
  return n == len;
}

static int buildMonthToIndex(const char* mon) {
  if (!mon)
    return 0;
  if (strncmp(mon, "Jan", 3) == 0)
    return 0;
  if (strncmp(mon, "Feb", 3) == 0)
    return 1;
  if (strncmp(mon, "Mar", 3) == 0)
    return 2;
  if (strncmp(mon, "Apr", 3) == 0)
    return 3;
  if (strncmp(mon, "May", 3) == 0)
    return 4;
  if (strncmp(mon, "Jun", 3) == 0)
    return 5;
  if (strncmp(mon, "Jul", 3) == 0)
    return 6;
  if (strncmp(mon, "Aug", 3) == 0)
    return 7;
  if (strncmp(mon, "Sep", 3) == 0)
    return 8;
  if (strncmp(mon, "Oct", 3) == 0)
    return 9;
  if (strncmp(mon, "Nov", 3) == 0)
    return 10;
  if (strncmp(mon, "Dec", 3) == 0)
    return 11;
  return 0;
}

static bool isLeapYear(int y) {
  if ((y % 400) == 0)
    return true;
  if ((y % 100) == 0)
    return false;
  return (y % 4) == 0;
}

static int64_t computeBuildEpochUtc() {
  const char* d = __DATE__;
  const char* t = __TIME__;
  if (!d || !t)
    return 0;

  char monStr[4];
  monStr[0] = d[0];
  monStr[1] = d[1];
  monStr[2] = d[2];
  monStr[3] = 0;
  int mon = buildMonthToIndex(monStr);
  int day = atoi(d + 4);
  int year = atoi(d + 7);
  int hour = atoi(t);
  int min = atoi(t + 3);
  int sec = atoi(t + 6);

  if (year < 1970)
    return 0;

  int64_t days = 0;
  for (int y = 1970; y < year; ++y) {
    days += isLeapYear(y) ? 366 : 365;
  }

  static const int mdays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  for (int m = 0; m < mon; ++m) {
    days += mdays[m];
    if (m == 1 && isLeapYear(year)) {
      days += 1;
    }
  }

  days += (day - 1);
  return (days * 86400LL) + ((int64_t)hour * 3600LL) + ((int64_t)min * 60LL) + (int64_t)sec;
}

static bool queryNtpUnixEpoch(const char* server, uint32_t timeoutMs, int64_t& outUnixEpochSec) {
  outUnixEpochSec = 0;
  if (!server || server[0] == 0) {
    return false;
  }

  IPAddress serverIp;
  if (!WiFi.hostByName(server, serverIp)) {
    return false;
  }

  WiFiUDP udp;
  if (udp.begin(0) == 0) {
    return false;
  }

  uint8_t packet[48];
  memset(packet, 0, sizeof(packet));
  packet[0] = 0b11100011;
  packet[1] = 0;
  packet[2] = 6;
  packet[3] = 0xEC;
  packet[12] = 49;
  packet[13] = 0x4E;
  packet[14] = 49;
  packet[15] = 52;

  udp.beginPacket(serverIp, 123);
  udp.write(packet, sizeof(packet));
  udp.endPacket();

  uint32_t start = millis();
  while ((millis() - start) < timeoutMs) {
    int packetSize = udp.parsePacket();
    if (packetSize >= 48) {
      int len = udp.read(packet, sizeof(packet));
      if (len < 48) {
        return false;
      }

      uint32_t secs1900 = ((uint32_t)packet[40] << 24) | ((uint32_t)packet[41] << 16) | ((uint32_t)packet[42] << 8) |
                          (uint32_t)packet[43];
      const uint32_t kUnixOffset = 2208988800UL;
      if (secs1900 < kUnixOffset) {
        return false;
      }
      outUnixEpochSec = (int64_t)(secs1900 - kUnixOffset);
      return true;
    }
    delay(10);
  }

  return false;
}

UIManager::UIManager(EInkDisplay& display, SDCardManager& sdManager, Buttons& buttons)
    : display(display), sdManager(sdManager), textRenderer(display), buttons(buttons) {
  // Initialize consolidated settings manager
  settings = new Settings(sdManager);
  // Create concrete screens and store pointers in the array.
  screens[ScreenId::FileBrowser] =
      std::unique_ptr<Screen>(new FileBrowserScreen(display, textRenderer, sdManager, *this));
  screens[ScreenId::ImageViewer] =
      std::unique_ptr<Screen>(new ImageViewerScreen(display, *this));
  screens[ScreenId::TextViewer] =
      std::unique_ptr<Screen>(new TextViewerScreen(display, textRenderer, sdManager, *this));
  screens[ScreenId::XtcViewer] =
      std::unique_ptr<Screen>(new XtcViewerScreen(display, textRenderer, sdManager, *this));
  screens[ScreenId::Settings] = std::unique_ptr<Screen>(new SettingsScreen(display, textRenderer, *this));
  screens[ScreenId::Chapters] = std::unique_ptr<Screen>(new ChaptersScreen(display, textRenderer, *this));
  screens[ScreenId::ClockSettings] =
      std::unique_ptr<Screen>(new ClockSettingsScreen(display, textRenderer, *this));
  screens[ScreenId::WifiSettings] =
      std::unique_ptr<Screen>(new WifiSettingsScreen(display, textRenderer, *this));
  screens[ScreenId::WifiSsidSelect] =
      std::unique_ptr<Screen>(new WifiSsidSelectScreen(display, textRenderer, *this));
  screens[ScreenId::WifiPasswordEntry] =
      std::unique_ptr<Screen>(new WifiPasswordEntryScreen(display, textRenderer, *this));
  screens[ScreenId::TimezoneSelect] =
      std::unique_ptr<Screen>(new TimezoneSelectScreen(display, textRenderer, *this));
  Serial.printf("[%lu] UIManager: Constructor called\n", millis());
}

UIManager::~UIManager() {
  if (settings)
    delete settings;
}

void UIManager::begin() {
  Serial.printf("[%lu] UIManager: begin() called\n", millis());
  // Load consolidated settings (import legacy files on first run)
  if (sdManager.ready()) {
    if (settings)
      settings->load();
  }

  // Restore soft clock (HH:MM) from consolidated settings
  if (sdManager.ready() && settings) {
    int savedH = 0;
    int savedM = 0;
    if (settings->getInt(String("clock.hour"), savedH) && settings->getInt(String("clock.minute"), savedM)) {
      setClockHM(savedH, savedM);
    }
  }
  // Initialize screens using generic Screen interface
  for (auto it = screens.begin(); it != screens.end(); ++it) {
    Screen* p = it->second.get();
    if (p)
      p->begin();
  }

  // Restore last-visible screen (use consolidated settings when available)
  currentScreen = ScreenId::FileBrowser;
  ScreenId savedPreviousScreen = ScreenId::FileBrowser;

  // Startup behavior: 0=Home, 1=Resume last (default)
  int startupBehavior = 1;
  if (sdManager.ready() && settings) {
    (void)settings->getInt(String("settings.startupBehavior"), startupBehavior);
  }

  if (sdManager.ready() && settings) {
    if (startupBehavior == 0) {
      Serial.printf("[%lu] UIManager: Startup behavior set to Home; ignoring saved screen\n", millis());
      currentScreen = ScreenId::FileBrowser;
      savedPreviousScreen = ScreenId::FileBrowser;
    } else {
      int saved = 0;
      if (settings->getInt(String("ui.screen"), saved)) {
        if (saved >= 0 && saved < static_cast<int>(ScreenId::Count)) {
          currentScreen = static_cast<ScreenId>(saved);
          Serial.printf("[%lu] UIManager: Restored screen %d from settings\n", millis(), saved);
        } else {
          Serial.printf("[%lu] UIManager: Invalid saved screen %d; using default\n", millis(), saved);
        }
      } else {
        Serial.printf("[%lu] UIManager: No saved screen state found; using default\n", millis());
      }

      // Restore previous screen (will apply after showScreen)
      int prevSaved = 0;
      if (settings->getInt(String("ui.previousScreen"), prevSaved)) {
        if (prevSaved >= 0 && prevSaved < static_cast<int>(ScreenId::Count)) {
          savedPreviousScreen = static_cast<ScreenId>(prevSaved);
          Serial.printf("[%lu] UIManager: Restored previous screen %d from settings\n", millis(), prevSaved);
        }
      }
    }
  } else {
    Serial.printf("[%lu] UIManager: SD not ready; using default start screen\n", millis());
  }

  showScreen(currentScreen);

  // Apply saved previousScreen after showScreen (which modifies previousScreen)
  previousScreen = savedPreviousScreen;

  startAutoNtpSyncIfEnabled();

  Serial.printf("[%lu] UIManager initialized\n", millis());
}

void UIManager::ntpSyncTaskTrampoline(void* param) {
  UIManager* self = static_cast<UIManager*>(param);
  vTaskDelay(5000 / portTICK_PERIOD_MS);

  for (int attempt = 0; attempt < 3; ++attempt) {
    self->trySyncTimeFromNtp();
    if (self->ntpTimeValid) {
      break;
    }
    vTaskDelay((3000 * (attempt + 1)) / portTICK_PERIOD_MS);
  }
  self->ntpSyncInProgress = false;
  self->ntpSyncTaskHandle = nullptr;
  vTaskDelete(nullptr);
}

void UIManager::startAutoNtpSyncIfEnabled() {
  if (!sdManager.ready() || !settings) {
    return;
  }
  if (ntpSyncInProgress) {
    return;
  }
  int wifiEnabled = 0;
  (void)settings->getInt(String("wifi.enabled"), wifiEnabled);
  if (wifiEnabled == 0) {
    return;
  }
  String ssid = settings->getString(String("wifi.ssid"));
  if (ssid.length() == 0) {
    return;
  }
  ntpSyncInProgress = true;
  xTaskCreate(&UIManager::ntpSyncTaskTrampoline, "NtpSync", 8192, this, 1, &ntpSyncTaskHandle);
}

void UIManager::handleButtons() {
  // Pass buttons to the current screen
  // Directly forward to the active screen (must exist)
  screens[currentScreen]->handleButtons(buttons);
}

void UIManager::showSleepScreen() {
  Serial.printf("[%lu] Showing SLEEP screen\n", millis());
  
  // We need to be careful with double buffering. 
  // clearScreen affects the current back buffer.
  display.clearScreen(0xFF);

  textRenderer.setFrameBuffer(display.getFrameBuffer());
  textRenderer.setBitmapType(TextRenderer::BITMAP_BW);
  textRenderer.setTextColor(TextRenderer::COLOR_BLACK);
  textRenderer.setFont(getMainFont());
  {
    const char* msg = "Going to sleep...";
    int16_t x1, y1;
    uint16_t w, h;
    textRenderer.getTextBounds(msg, 0, 0, &x1, &y1, &w, &h);
    int16_t cx = ((int16_t)EInkDisplay::DISPLAY_WIDTH - (int)w) / 2;
    int16_t cy = ((int16_t)EInkDisplay::DISPLAY_HEIGHT - (int)h) / 2;
    textRenderer.setCursor(cx, cy);
    textRenderer.print(msg);
  }
  display.displayBuffer(EInkDisplay::FAST_REFRESH);

  display.clearScreen(0xFF);

  bool usedRandomCover = false;
  bool haveDecodedGrayscale = false;
  uint8_t* decodedGrayLsb = nullptr;
  uint8_t* decodedGrayMsb = nullptr;
  bool coverIsBmp = false;
  String coverBmpPath;
  bool wantCacheBundle = false;
  bool wroteCacheBw = false;
  bool wroteCacheLsb = false;
  bool wroteCacheMsb = false;
  String cacheBase;
  String cacheGcvPath;
  String cacheBwPath;
  String cacheLsbPath;
  String cacheMsbPath;
  int sleepMode = 0;
  if (settings) {
    (void)settings->getInt(String("settings.sleepScreenMode"), sleepMode);
  }

  if (sleepMode == 0 && settings) {
    String coverPath = settings->getString(String("textviewer.lastCoverPath"), String(""));
    if (coverPath.length() > 0) {
      // SD and the e-ink controller share SPI; ensure CS lines are in a safe state
      // before we touch SD during shutdown.
      sdManager.ensureSpiBusIdle();
    }

    // If caller saved a non-extracting cover path, it may not exist yet.
    // Try to extract it on-demand using the last EPUB path.
    if (coverPath.length() > 0 && !SD.exists(coverPath.c_str())) {
      const String lf = String(coverPath).substring(std::max(0, (int)coverPath.length() - 4));
      const bool maybeCoverFile = coverPath.indexOf(String("/microreader/epub_")) == 0;
      if (maybeCoverFile) {
        const String epubPath = settings->getString(String("textviewer.lastEpubPath"), String(""));
        if (epubPath.length() > 0 && SD.exists(epubPath.c_str())) {
          Serial.printf("[%lu]   Sleep cover missing on disk; extracting on-demand...\n", millis());
          // Keep this conservative: extraction allocates; skip if heap is extremely low.
          // Use MALLOC_CAP_INTERNAL to avoid walking 8MB PSRAM which triggers WDT on ESP32-S3
          const uint32_t freeHeap = ESP.getFreeHeap();
          const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
          if (freeHeap > 90000 && largest > 60000) {
            sdManager.ensureSpiBusIdle();
            EpubReader er(epubPath.c_str());
            if (er.isValid()) {
              String extracted = er.getCoverImagePath(true);
              if (extracted.length() > 0 && SD.exists(extracted.c_str())) {
                coverPath = extracted;
                settings->setString(String("textviewer.lastCoverPath"), coverPath);
                (void)settings->save();
              }
            }
          }
        }
      }
    }

    if (coverPath.length() > 0 && SD.exists(coverPath.c_str())) {
      Serial.printf("Selecting book cover sleep screen: %s\n", coverPath.c_str());

      String lf = coverPath;
      lf.toLowerCase();

      // Fast path: cached raw grayscale cover bundle.
      // Marker file: /microreader/epub_covers/<key>.gcv
      // Data files:  /microreader/epub_covers/<key>.bw/.lsb/.msb (each 48000 bytes)
      if (lf.endsWith(".gcv")) {
        String base = coverPath;
        base.remove(base.length() - 4);
        const String bwPath = base + String(".bw");
        const String lsbPath = base + String(".lsb");
        const String msbPath = base + String(".msb");

        if (SD.exists(bwPath.c_str()) && SD.exists(lsbPath.c_str()) && SD.exists(msbPath.c_str())) {
          sdManager.ensureSpiBusIdle();
          File bw = SD.open(bwPath.c_str(), FILE_READ);
          if (bw) {
            const size_t n = bw.read(display.getFrameBuffer(), EInkDisplay::BUFFER_SIZE);
            bw.close();
            if (n == EInkDisplay::BUFFER_SIZE) {
              usedRandomCover = true;
              coverBmpPath = coverPath;
              coverIsBmp = false;
            } else {
              Serial.printf("[%lu]   Sleep cover cached BW read short (%u/%u)\n", millis(), (unsigned)n,
                            (unsigned)EInkDisplay::BUFFER_SIZE);
            }
          }
        }
      }

      if (!usedRandomCover) {
        coverBmpPath = coverPath;
        coverIsBmp = lf.endsWith(".bmp");

        // JPEG/PNG decode can allocate internally; if heap is extremely low, skip decode.
        // Use MALLOC_CAP_INTERNAL to avoid walking 8MB PSRAM which triggers WDT on ESP32-S3
        const uint32_t freeHeap = ESP.getFreeHeap();
        const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        Serial.printf("[%lu]   Sleep cover heap before decode: Free=%u\n", millis(), (unsigned)freeHeap);
        Serial.printf("[%lu]   Sleep cover heap largest free block: %u\n", millis(), (unsigned)largest);
        if (freeHeap < 60000) {
          Serial.printf("Skipping book cover sleep screen decode due to low heap (Free=%u)\n", (unsigned)freeHeap);
        } else {
          // Guard against heap fragmentation: JPEG/PNG decode can require a contiguous block.
          // If we don't have enough contiguous memory, skip cover instead of crashing.
          if (!coverIsBmp && !lf.endsWith(".gcv") && largest < 80000) {
            Serial.printf("Skipping book cover sleep screen decode due to fragmented heap (Largest=%u)\n",
                          (unsigned)largest);
          } else {
          // Decode BW only here. Grayscale overlay is applied later.
          if (ImageDecoder::decodeToDisplayFitWidth(coverPath.c_str(), display.getFrameBuffer(), EInkDisplay::DISPLAY_WIDTH, EInkDisplay::DISPLAY_HEIGHT)) {
            usedRandomCover = true;

            // If this is a JPG/PNG cover, prepare a fast raw bundle cache during sleep.
            // We do it here (instead of EPUB-open) to avoid stability issues during open.
            if (!coverIsBmp && !lf.endsWith(".gcv")) {
              const bool isJpgPng = lf.endsWith(".jpg") || lf.endsWith(".jpeg") || lf.endsWith(".png");
              if (isJpgPng) {
                (void)sdManager.ensureDirectoryExists("/microreader/epub_covers");
                const uint32_t key = fnv1a32_ui(coverPath.c_str());
                cacheBase = String("/microreader/epub_covers/") + String(key, HEX);
                cacheGcvPath = cacheBase + String(".gcv");
                cacheBwPath = cacheBase + String(".bw");
                cacheLsbPath = cacheBase + String(".lsb");
                cacheMsbPath = cacheBase + String(".msb");

                // Only build cache if not already present.
                if (!(SD.exists(cacheGcvPath.c_str()) && SD.exists(cacheBwPath.c_str()) && SD.exists(cacheLsbPath.c_str()) &&
                      SD.exists(cacheMsbPath.c_str()))) {
                  wantCacheBundle = true;
                  sdManager.ensureSpiBusIdle();
                  wroteCacheBw = writeRawBufferToFile_ui(cacheBwPath.c_str(), display.getFrameBuffer(), EInkDisplay::BUFFER_SIZE);
                  if (!wroteCacheBw) {
                    Serial.printf("[%lu]   Sleep cover cache: failed writing BW %s\n", millis(), cacheBwPath.c_str());
                    wantCacheBundle = false;
                  } else {
                    Serial.printf("[%lu]   Sleep cover cache: wrote BW %s\n", millis(), cacheBwPath.c_str());
                  }
                }
              }
            }
          } else {
            Serial.println("Failed to decode book cover sleep screen");
          }
          }
        }
      }
    }
  } else if (sleepMode == 1) {
    auto files = sdManager.listFiles("/images", 50);
    std::vector<String> images;
    for (const auto& f : files) {
      String lf = f;
      lf.toLowerCase();
      if (lf.startsWith("._")) continue;
      if (lf.endsWith(".jpg") || lf.endsWith(".jpeg") || lf.endsWith(".png") || lf.endsWith(".bmp")) {
        images.push_back(f);
      }
    }

    if (!images.empty()) {
      auto bmpIsSupported = [](const String& fullPath) -> bool {
        String lf = fullPath;
        lf.toLowerCase();
        if (!lf.endsWith(".bmp")) {
          return true;
        }
        File f = SD.open(fullPath.c_str(), FILE_READ);
        if (!f) {
          return false;
        }
        uint8_t hdr[54];
        int n = (int)f.read(hdr, sizeof(hdr));
        f.close();
        if (n != (int)sizeof(hdr)) {
          return false;
        }
        if (hdr[0] != 'B' || hdr[1] != 'M') {
          return false;
        }
        uint32_t compression = (uint32_t)hdr[30] | ((uint32_t)hdr[31] << 8) | ((uint32_t)hdr[32] << 16) | ((uint32_t)hdr[33] << 24);
        return compression == 0;
      };

      const int maxAttempts = (int)std::min<size_t>(images.size(), 6);
      for (int attempt = 0; attempt < maxAttempts && !usedRandomCover; ++attempt) {
        uint32_t r = esp_random();
        int idx = (int)(r % images.size());

        // Avoid immediately repeating the same image across deep sleep cycles.
        if ((int)images.size() > 1 && idx == g_lastSleepCoverIndex) {
          idx = (idx + 1 + (int)((r >> 16) % (images.size() - 1))) % images.size();
        }

        String selected = String("/images/") + images[idx];
        if (!bmpIsSupported(selected)) {
          Serial.printf("Skipping unsupported BMP sleep cover: %s\n", selected.c_str());
          continue;
        }

        Serial.printf("Selecting random sleep cover: %s\n", selected.c_str());

        // decodeToDisplay writes directly to the buffer we pass it.
        // We pass the current back buffer (which display.getFrameBuffer() returns).
        const uint32_t freeHeap = ESP.getFreeHeap();
        uint8_t* grayLsb = nullptr;
        uint8_t* grayMsb = nullptr;
        if (display.supportsGrayscale() && freeHeap > 180000) {
          grayLsb = (uint8_t*)malloc(EInkDisplay::BUFFER_SIZE);
          grayMsb = (uint8_t*)malloc(EInkDisplay::BUFFER_SIZE);
          if (!grayLsb || !grayMsb) {
            free(grayLsb);
            free(grayMsb);
            grayLsb = nullptr;
            grayMsb = nullptr;
          } else {
            memset(grayLsb, 0xFF, EInkDisplay::BUFFER_SIZE);
            memset(grayMsb, 0xFF, EInkDisplay::BUFFER_SIZE);
          }
        }

        if (ImageDecoder::decodeToDisplay(selected.c_str(), display.getFrameBuffer(), EInkDisplay::DISPLAY_WIDTH, EInkDisplay::DISPLAY_HEIGHT, grayLsb, grayMsb)) {
          usedRandomCover = true;
          g_lastSleepCoverIndex = idx;

          if (grayLsb && grayMsb) {
            haveDecodedGrayscale = true;
            decodedGrayLsb = grayLsb;
            decodedGrayMsb = grayMsb;
            grayLsb = nullptr;
            grayMsb = nullptr;
          }

          free(grayLsb);
          free(grayMsb);
          break;
        }

        free(grayLsb);
        free(grayMsb);
        Serial.println("Failed to decode random sleep cover");
      }
    }
  }

  if (!usedRandomCover) {
    // Draw bebop image centered as fallback
    display.drawImage(bebop_image, 0, 0, BEBOP_IMAGE_WIDTH, BEBOP_IMAGE_HEIGHT, true);
  }

  // Add "Sleeping..." text at the bottom (into the same back buffer)
  {
    textRenderer.setFrameBuffer(display.getFrameBuffer());
    textRenderer.setBitmapType(TextRenderer::BITMAP_BW);
    textRenderer.setTextColor(TextRenderer::COLOR_BLACK);
    textRenderer.setFont(getMainFont());

    const char* sleepText = "Sleeping...";
    int16_t x1, y1;
    uint16_t w, h;
    textRenderer.getTextBounds(sleepText, 0, 0, &x1, &y1, &w, &h);
    int16_t centerX = ((int16_t)EInkDisplay::DISPLAY_WIDTH - w) / 2;

    textRenderer.setCursor(centerX, EInkDisplay::DISPLAY_HEIGHT - 20);
    textRenderer.print(sleepText);
  }

  // displayBuffer sends the back buffer to the controller AND swaps pointers.
  // After this call, the buffer containing the image is now the "active" front buffer.
  display.displayBuffer(EInkDisplay::FULL_REFRESH);
  
  // Note: Since displayBuffer() calls swapBuffers(), any subsequent drawing 
  // without another clearScreen/swap would happen on the old front buffer.
  // But we are entering deep sleep, so this is the final state.
  
  if (display.supportsGrayscale()) {
    // If we decoded grayscale buffers earlier, copy them AFTER the BW full refresh.
    // displayBuffer() writes RED RAM and can overwrite any earlier grayscale plane copies.
    if (haveDecodedGrayscale && decodedGrayLsb && decodedGrayMsb) {
      display.copyGrayscaleBuffers(decodedGrayLsb, decodedGrayMsb);
      display.displayGrayBuffer(true);
    } else if (usedRandomCover && coverBmpPath.length() > 0) {
      String lf = coverBmpPath;
      lf.toLowerCase();

      if (lf.endsWith(".gcv")) {
        String base = coverBmpPath;
        base.remove(base.length() - 4);
        const String lsbPath = base + String(".lsb");
        const String msbPath = base + String(".msb");

        uint8_t* mask = (uint8_t*)heap_caps_malloc(EInkDisplay::BUFFER_SIZE, MALLOC_CAP_8BIT);
        if (mask) {
          File f = SD.open(lsbPath.c_str(), FILE_READ);
          if (f) {
            const size_t n = f.read(mask, EInkDisplay::BUFFER_SIZE);
            f.close();
            if (n == EInkDisplay::BUFFER_SIZE) {
              display.copyGrayscaleLsbBuffers(mask);
            } else {
              Serial.printf("[%lu]   Sleep cover cached LSB read short (%u/%u)\n", millis(), (unsigned)n,
                            (unsigned)EInkDisplay::BUFFER_SIZE);
            }
          }
          free(mask);
        }

        mask = (uint8_t*)heap_caps_malloc(EInkDisplay::BUFFER_SIZE, MALLOC_CAP_8BIT);
        if (mask) {
          File f = SD.open(msbPath.c_str(), FILE_READ);
          if (f) {
            const size_t n = f.read(mask, EInkDisplay::BUFFER_SIZE);
            f.close();
            if (n == EInkDisplay::BUFFER_SIZE) {
              display.copyGrayscaleMsbBuffers(mask);
            } else {
              Serial.printf("[%lu]   Sleep cover cached MSB read short (%u/%u)\n", millis(), (unsigned)n,
                            (unsigned)EInkDisplay::BUFFER_SIZE);
            }
          }
          free(mask);
        }

        display.displayGrayBuffer(true);
      } else if (coverIsBmp) {
      // Crosspoint-style grayscale pipeline for BMP covers without extra 48KB allocations:
      // decode plane into framebuffer, copy to display RAM, repeat for MSB.
      display.clearScreen(0x00);
      if (ImageDecoder::decodeBmpPlaneFitWidth(coverBmpPath.c_str(), display.getFrameBuffer(), EInkDisplay::DISPLAY_WIDTH, EInkDisplay::DISPLAY_HEIGHT, 0x01)) {
        display.copyGrayscaleLsbBuffers(display.getFrameBuffer());
      }
      display.clearScreen(0x00);
      if (ImageDecoder::decodeBmpPlaneFitWidth(coverBmpPath.c_str(), display.getFrameBuffer(), EInkDisplay::DISPLAY_WIDTH, EInkDisplay::DISPLAY_HEIGHT, 0x02)) {
        display.copyGrayscaleMsbBuffers(display.getFrameBuffer());
      }
      display.displayGrayBuffer(true);
      } else {
      // Heap-fragmentation-safe grayscale path for JPG/PNG:
      // allocate one 48KB mask buffer at a time, decode into it, copy to RAM, free, repeat.
      uint8_t* mask = nullptr;

      Serial.printf("[%lu]   Sleep grayscale (JPG/PNG) LSB decode start\n", millis());
      mask = (uint8_t*)heap_caps_malloc(EInkDisplay::BUFFER_SIZE, MALLOC_CAP_8BIT);
      if (mask) {
        memset(mask, 0x00, EInkDisplay::BUFFER_SIZE);
        // Decode fills mask semantics (LSB=dark only) when only grayscaleLsbBuffer is provided.
        (void)ImageDecoder::decodeToDisplayFitWidth(coverBmpPath.c_str(), display.getFrameBuffer(), EInkDisplay::DISPLAY_WIDTH, EInkDisplay::DISPLAY_HEIGHT, mask, nullptr);
        display.copyGrayscaleLsbBuffers(mask);

        if (wantCacheBundle) {
          sdManager.ensureSpiBusIdle();
          wroteCacheLsb = writeRawBufferToFile_ui(cacheLsbPath.c_str(), mask, EInkDisplay::BUFFER_SIZE);
          if (!wroteCacheLsb) {
            Serial.printf("[%lu]   Sleep cover cache: failed writing LSB %s\n", millis(), cacheLsbPath.c_str());
          } else {
            Serial.printf("[%lu]   Sleep cover cache: wrote LSB %s\n", millis(), cacheLsbPath.c_str());
          }
        }

        free(mask);
      } else {
        Serial.printf("[%lu]   Sleep grayscale (JPG/PNG) LSB mask alloc failed\n", millis());
      }
      Serial.printf("[%lu]   Sleep grayscale (JPG/PNG) LSB decode done\n", millis());

      Serial.printf("[%lu]   Sleep grayscale (JPG/PNG) MSB decode start\n", millis());
      mask = (uint8_t*)heap_caps_malloc(EInkDisplay::BUFFER_SIZE, MALLOC_CAP_8BIT);
      if (mask) {
        memset(mask, 0x00, EInkDisplay::BUFFER_SIZE);
        // Decode fills mask semantics (MSB=dark+light) when only grayscaleMsbBuffer is provided.
        (void)ImageDecoder::decodeToDisplayFitWidth(coverBmpPath.c_str(), display.getFrameBuffer(), EInkDisplay::DISPLAY_WIDTH, EInkDisplay::DISPLAY_HEIGHT, nullptr, mask);
        display.copyGrayscaleMsbBuffers(mask);

        if (wantCacheBundle) {
          sdManager.ensureSpiBusIdle();
          wroteCacheMsb = writeRawBufferToFile_ui(cacheMsbPath.c_str(), mask, EInkDisplay::BUFFER_SIZE);
          if (!wroteCacheMsb) {
            Serial.printf("[%lu]   Sleep cover cache: failed writing MSB %s\n", millis(), cacheMsbPath.c_str());
          } else {
            Serial.printf("[%lu]   Sleep cover cache: wrote MSB %s\n", millis(), cacheMsbPath.c_str());
          }
        }

        free(mask);
      } else {
        Serial.printf("[%lu]   Sleep grayscale (JPG/PNG) MSB mask alloc failed\n", millis());
      }
      Serial.printf("[%lu]   Sleep grayscale (JPG/PNG) MSB decode done\n", millis());

      if (wantCacheBundle && wroteCacheBw && wroteCacheLsb && wroteCacheMsb) {
        sdManager.ensureSpiBusIdle();
        if (SD.exists(cacheGcvPath.c_str())) {
          SD.remove(cacheGcvPath.c_str());
        }
        File marker = SD.open(cacheGcvPath.c_str(), FILE_WRITE);
        if (marker) {
          marker.close();
        }
        if (SD.exists(cacheGcvPath.c_str())) {
          Serial.printf("[%lu]   Sleep cover cache: wrote marker %s\n", millis(), cacheGcvPath.c_str());
          if (settings) {
            settings->setString(String("textviewer.lastCoverPath"), cacheGcvPath);
            (void)settings->save();
          }
        } else {
          Serial.printf("[%lu]   Sleep cover cache: failed writing marker %s\n", millis(), cacheGcvPath.c_str());
        }
      }

      Serial.printf("[%lu]   Sleep grayscale displayGrayBuffer start\n", millis());
      display.displayGrayBuffer(true);
      Serial.printf("[%lu]   Sleep grayscale displayGrayBuffer done\n", millis());
      }
    } else if (!usedRandomCover) {
      display.copyGrayscaleBuffers(bebop_image_lsb, bebop_image_msb);
      display.displayGrayBuffer(true);
    }
  }

  free(decodedGrayLsb);
  free(decodedGrayMsb);
}

void UIManager::prepareForSleep() {
  // Notify the active screen that the device is powering down so it can
  // persist any state (e.g. current reading position).
  if (screens[currentScreen])
    screens[currentScreen]->shutdown();

  // After state is persisted, aggressively free reader resources so the sleep
  // screen cover render has as much heap as possible.
  if (currentScreen == ScreenId::TextViewer) {
    TextViewerScreen* tv = static_cast<TextViewerScreen*>(screens[ScreenId::TextViewer].get());
    if (tv) {
      tv->closeDocument();
    }
  }
  if (currentScreen == ScreenId::XtcViewer) {
    XtcViewerScreen* xv = static_cast<XtcViewerScreen*>(screens[ScreenId::XtcViewer].get());
    if (xv) {
      xv->closeDocument();
    }
  }
  // Persist which screen was active so we can restore it on next boot.
  if (sdManager.ready() && settings) {
    settings->setInt(String("ui.screen"), static_cast<int>(currentScreen));
    settings->setInt(String("ui.previousScreen"), static_cast<int>(previousScreen));

    // Persist soft clock value (HH:MM)
    int h = 0;
    int m = 0;
    if (getClockHM(h, m)) {
      settings->setInt(String("clock.hour"), h);
      settings->setInt(String("clock.minute"), m);
    }

    if (!settings->save()) {
      Serial.println("UIManager: Failed to write settings.cfg to SD");
    }
  } else {
    Serial.println("UIManager: SD not ready; skipping save of current screen");
  }
}

void UIManager::setClockHM(int hour, int minute) {
  if (hour < 0)
    hour = 0;
  if (hour > 23)
    hour = 23;
  if (minute < 0)
    minute = 0;
  if (minute > 59)
    minute = 59;
  clockBaseMinutes = (hour * 60) + minute;
  clockBaseMillis = millis();
  clockValid = true;
}

bool UIManager::getClockHM(int& hourOut, int& minuteOut) {
  if (!clockValid) {
    return false;
  }
  uint32_t now = millis();
  uint32_t elapsedMs = now - clockBaseMillis;
  int32_t addMinutes = (int32_t)(elapsedMs / 60000UL);
  int32_t curMinutes = clockBaseMinutes + addMinutes;
  curMinutes %= (24 * 60);
  if (curMinutes < 0)
    curMinutes += (24 * 60);
  hourOut = (int)(curMinutes / 60);
  minuteOut = (int)(curMinutes % 60);
  return true;
}

String UIManager::getClockString() {
  if (!ntpTimeValid) {
    return String("--:--");
  }

  int h = 0;
  int m = 0;
  if (getClockHM(h, m)) {
    char buf[6];
    snprintf(buf, sizeof(buf), "%02d:%02d", h, m);
    return String(buf);
  }

  ntpTimeValid = false;
  return String("--:--");
}

void UIManager::renderStatusHeader(TextRenderer& renderer) {
  renderer.setFont(&MenuFontSmall);

  {
    String t = getClockString();
    renderer.setCursor(10, 35);
    renderer.print(t);
  }

  int pct = (int)g_battery.readPercentage();
  if (pct < 0)
    pct = 0;
  if (pct > 100)
    pct = 100;
  String pctStr = String(pct) + "%";

  int16_t tx1, ty1;
  uint16_t tw, th;
  renderer.getTextBounds(pctStr.c_str(), 0, 0, &tx1, &ty1, &tw, &th);

  const int16_t marginRight = 10;
  const int16_t baselineY = 35;
  const int16_t iconW = 22;
  const int16_t iconH = 12;
  const int16_t nubW = 3;
  const int16_t nubH = 6;
  const int16_t gap = 6;

  int16_t groupW = iconW + gap + (int16_t)tw;
  const int16_t pageW = (int16_t)EInkDisplay::DISPLAY_WIDTH;
  int16_t groupX = pageW - marginRight - groupW;
  if (groupX < 0)
    groupX = 0;

  int16_t iconX = groupX;
  int16_t iconTop = baselineY - iconH + 1;
  int16_t textX = iconX + iconW + gap;

  for (int16_t x = 0; x < iconW; ++x) {
    renderer.drawPixel(iconX + x, iconTop, true);
    renderer.drawPixel(iconX + x, iconTop + iconH - 1, true);
  }
  for (int16_t y = 0; y < iconH; ++y) {
    renderer.drawPixel(iconX, iconTop + y, true);
    renderer.drawPixel(iconX + iconW - 1, iconTop + y, true);
  }

  int16_t nubX = iconX + iconW;
  int16_t nubTop = iconTop + (iconH - nubH) / 2;
  for (int16_t x = 0; x < nubW; ++x) {
    for (int16_t y = 0; y < nubH; ++y) {
      renderer.drawPixel(nubX + x, nubTop + y, true);
    }
  }

  int16_t innerW = iconW - 2;
  int16_t fillW = (int16_t)((innerW * pct) / 100);
  if (fillW < 0)
    fillW = 0;
  if (fillW > innerW)
    fillW = innerW;
  for (int16_t x = 0; x < fillW; ++x) {
    for (int16_t y = 0; y < iconH - 2; ++y) {
      renderer.drawPixel(iconX + 1 + x, iconTop + 1 + y, true);
    }
  }

  renderer.setCursor(textX, baselineY);
  renderer.print(pctStr);
}

void UIManager::trySyncTimeFromNtp() {
  ntpTimeValid = false;
  if (!sdManager.ready() || !settings) {
    return;
  }

  // WiFi/TLS may require large contiguous allocations. Release shared EPUB
  // decompression buffers to maximize available heap.
  epub_release_shared_buffers();

  int wifiEnabled = 0;
  (void)settings->getInt(String("wifi.enabled"), wifiEnabled);
  if (wifiEnabled == 0) {
    return;
  }

  String ssid = settings->getString(String("wifi.ssid"));
  String pass = settings->getString(String("wifi.pass"));
  if (ssid.length() == 0) {
    Serial.println("UIManager: WiFi enabled but wifi.ssid missing");
    return;
  }

  int gmtOffset = 0;
  int daylightOffset = 0;
  (void)settings->getInt(String("wifi.gmtOffset"), gmtOffset);
  (void)settings->getInt(String("wifi.daylightOffset"), daylightOffset);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.disconnect(true);
  delay(100);

  Serial.printf("UIManager: WiFi connecting to '%s'...\n", ssid.c_str());
  WiFi.begin(ssid.c_str(), pass.c_str());

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < 8000) {
    delay(50);
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf("UIManager: WiFi connect failed (status=%d)\n", (int)WiFi.status());
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    return;
  }

  Serial.printf("UIManager: WiFi connected, IP=%s\n", WiFi.localIP().toString().c_str());

  const int64_t kMinEpoch2026 = 1767225600LL;
  const int64_t kMaxEpoch = 2147483647LL;
  int64_t minEpoch = kMinEpoch2026;
  int64_t buildEpoch = computeBuildEpochUtc();
  if (buildEpoch > 0) {
    int64_t floorEpoch = buildEpoch - 86400LL;
    if (floorEpoch > minEpoch) {
      minEpoch = floorEpoch;
    }
  }
  if (g_lastGoodEpochSec > 0) {
    int64_t floorEpoch = g_lastGoodEpochSec - 60LL;
    if (floorEpoch > minEpoch) {
      minEpoch = floorEpoch;
    }
  }

  bool gotValidTime = false;
  int64_t epochSec = 0;

  const char* servers[3] = {"pool.ntp.org", "time.google.com", "time.nist.gov"};
  for (int attempt = 0; attempt < 6 && !gotValidTime; ++attempt) {
    for (int s = 0; s < 3 && !gotValidTime; ++s) {
      int64_t epoch = 0;
      if (queryNtpUnixEpoch(servers[s], 2500, epoch)) {
        Serial.printf("UIManager: NTP reply from %s epoch=%lld min=%lld\n", servers[s], (long long)epoch,
                      (long long)minEpoch);
        if (epoch >= minEpoch && epoch <= kMaxEpoch) {
          epochSec = epoch;
          gotValidTime = true;
          break;
        }
      }
    }
    if (!gotValidTime) {
      delay(500);
    }
  }

  if (gotValidTime) {
    int64_t localSec = epochSec + (int64_t)gmtOffset + (int64_t)daylightOffset;
    int32_t minutes = (int32_t)(localSec / 60);
    int32_t dayMinutes = minutes % (24 * 60);
    if (dayMinutes < 0)
      dayMinutes += (24 * 60);
    int hour = (int)(dayMinutes / 60);
    int minute = (int)(dayMinutes % 60);

    setClockHM(hour, minute);
    ntpTimeValid = true;
    g_lastGoodEpochSec = epochSec;
    if (sdManager.ready() && settings) {
      settings->setInt(String("clock.hour"), hour);
      settings->setInt(String("clock.minute"), minute);
      settings->setInt(String("clock.lastEpoch"), (int)epochSec);
      if (!settings->save()) {
        Serial.println("UIManager: Failed to persist synced clock to settings.cfg");
      }
    }
    switch (currentScreen) {
      case ScreenId::FileBrowser:
      case ScreenId::Settings:
      case ScreenId::Chapters:
      case ScreenId::ClockSettings:
      case ScreenId::WifiSettings:
      case ScreenId::WifiSsidSelect:
      case ScreenId::WifiPasswordEntry:
      case ScreenId::TimezoneSelect:
        if (screens[currentScreen]) {
          screens[currentScreen]->show();
        }
        break;
      default:
        break;
    }
    Serial.printf("UIManager: NTP time synced (epoch=%lld)\n", (long long)epochSec);
  } else {
    Serial.println("UIManager: NTP sync failed (invalid time)");
    ntpTimeValid = false;
  }

  // We no longer need WiFi after initial sync.
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

void UIManager::openTextFile(const String& sdPath) {
  Serial.printf("UIManager: openTextFile %s\n", sdPath.c_str());

  {
    String lf = sdPath;
    lf.toLowerCase();
    if (lf.endsWith(".xtc") || lf.endsWith(".xtch")) {
      openXtcFile(sdPath);
      return;
    }
  }

  display.clearScreen(0xFF);
  textRenderer.setFrameBuffer(display.getFrameBuffer());
  textRenderer.setBitmapType(TextRenderer::BITMAP_BW);
  textRenderer.setTextColor(TextRenderer::COLOR_BLACK);
  textRenderer.setFont(getMainFont());

  {
    const char* l1 = "Loading...";
    const char* l2 = "(please wait)";
    int16_t x1, y1;
    uint16_t w1, h1;
    uint16_t w2, h2;
    textRenderer.getTextBounds(l1, 0, 0, &x1, &y1, &w1, &h1);
    textRenderer.getTextBounds(l2, 0, 0, &x1, &y1, &w2, &h2);
    const int16_t lineGap = 8;
    int16_t totalH = (int16_t)h1 + lineGap + (int16_t)h2;
    int16_t startY = ((int16_t)EInkDisplay::DISPLAY_HEIGHT - totalH) / 2;
    int16_t cx1 = ((int16_t)EInkDisplay::DISPLAY_WIDTH - (int)w1) / 2;
    int16_t cx2 = ((int16_t)EInkDisplay::DISPLAY_WIDTH - (int)w2) / 2;
    textRenderer.setCursor(cx1, startY);
    textRenderer.print(l1);
    textRenderer.setCursor(cx2, startY + (int16_t)h1 + lineGap);
    textRenderer.print(l2);
  }

  display.displayBuffer(EInkDisplay::FAST_REFRESH);

  // Directly access TextViewerScreen and open the file (guaranteed to exist)
  static_cast<TextViewerScreen*>(screens[ScreenId::TextViewer].get())->openFile(sdPath);
  showScreen(ScreenId::TextViewer);
}

void UIManager::openXtcFile(const String& sdPath) {
  Serial.printf("UIManager: openXtcFile %s\n", sdPath.c_str());

  display.clearScreen(0xFF);
  textRenderer.setFrameBuffer(display.getFrameBuffer());
  textRenderer.setBitmapType(TextRenderer::BITMAP_BW);
  textRenderer.setTextColor(TextRenderer::COLOR_BLACK);
  textRenderer.setFont(getMainFont());

  {
    const char* l1 = "Loading...";
    const char* l2 = "(please wait)";
    int16_t x1, y1;
    uint16_t w1, h1;
    uint16_t w2, h2;
    textRenderer.getTextBounds(l1, 0, 0, &x1, &y1, &w1, &h1);
    textRenderer.getTextBounds(l2, 0, 0, &x1, &y1, &w2, &h2);
    const int16_t lineGap = 8;
    int16_t totalH = (int16_t)h1 + lineGap + (int16_t)h2;
    int16_t startY = ((int16_t)EInkDisplay::DISPLAY_HEIGHT - totalH) / 2;
    int16_t cx1 = ((int16_t)EInkDisplay::DISPLAY_WIDTH - (int)w1) / 2;
    int16_t cx2 = ((int16_t)EInkDisplay::DISPLAY_WIDTH - (int)w2) / 2;
    textRenderer.setCursor(cx1, startY);
    textRenderer.print(l1);
    textRenderer.setCursor(cx2, startY + (int16_t)h1 + lineGap);
    textRenderer.print(l2);
  }

  display.displayBuffer(EInkDisplay::FAST_REFRESH);

  static_cast<XtcViewerScreen*>(screens[ScreenId::XtcViewer].get())->openFile(sdPath);
  showScreen(ScreenId::XtcViewer);
}

bool UIManager::clearEpubCache() {
  if (!sdManager.ready()) {
    return false;
  }
  return sdManager.clearEpubExtractCache();
}

void UIManager::showScreen(ScreenId id) {
  // Directly show the requested screen (assumed present)
  if (id == ScreenId::Settings && currentScreen != ScreenId::Settings) {
    if (currentScreen != ScreenId::WifiSettings && currentScreen != ScreenId::WifiSsidSelect &&
        currentScreen != ScreenId::WifiPasswordEntry && currentScreen != ScreenId::ClockSettings &&
        currentScreen != ScreenId::TimezoneSelect && currentScreen != ScreenId::Chapters) {
      settingsReturnScreen = currentScreen;
    }
  }

  // Apply reading orientation only while in TextViewer/XtcViewer; keep UI screens in portrait.
  int orientation = 0;
  if ((id == ScreenId::TextViewer || id == ScreenId::XtcViewer) && settings) {
    (void)settings->getInt(String("settings.orientation"), orientation);
    switch (orientation) {
      case 0:
        textRenderer.setOrientation(TextRenderer::Portrait);
        break;
      case 1:
        textRenderer.setOrientation(TextRenderer::LandscapeClockwise);
        break;
      default:
        textRenderer.setOrientation(TextRenderer::Portrait);
        break;
    }
  } else {
    textRenderer.setOrientation(TextRenderer::Portrait);
  }
  // Update button tap zones for the current orientation
  buttons.setOrientation(orientation);

  previousScreen = currentScreen;
  currentScreen = id;
  // Call activate so screens can perform any work needed when they become
  // active (this also ensures TextViewerScreen::activate is invoked to open
  // any pending file that was loaded during begin()).
  screens[id]->activate();
  screens[id]->show();
}
