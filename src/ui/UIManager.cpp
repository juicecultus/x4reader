#include "UIManager.h"

#include <Arduino.h>
#include <resources/fonts/FontManager.h>

#include <WiFi.h>
#include <WiFiUdp.h>
#include <time.h>
#include <sys/time.h>

#include <esp_sntp.h>

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
#include "ui/screens/ClockSettingsScreen.h"
#include "ui/screens/TimezoneSelectScreen.h"
#include "ui/screens/WifiPasswordEntryScreen.h"
#include "ui/screens/WifiSettingsScreen.h"
#include "ui/screens/WifiSsidSelectScreen.h"

#include <resources/fonts/other/MenuFontSmall.h>

RTC_DATA_ATTR static int32_t g_lastSleepCoverIndex = -1;
RTC_DATA_ATTR static int64_t g_lastGoodEpochSec = 0;

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

UIManager::UIManager(EInkDisplay& display, SDCardManager& sdManager)
    : display(display), sdManager(sdManager), textRenderer(display) {
  // Initialize consolidated settings manager
  settings = new Settings(sdManager);
  // Create concrete screens and store pointers in the array.
  screens[ScreenId::FileBrowser] =
      std::unique_ptr<Screen>(new FileBrowserScreen(display, textRenderer, sdManager, *this));
  screens[ScreenId::ImageViewer] =
      std::unique_ptr<Screen>(new ImageViewerScreen(display, *this));
  screens[ScreenId::TextViewer] =
      std::unique_ptr<Screen>(new TextViewerScreen(display, textRenderer, sdManager, *this));
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

  if (sdManager.ready() && settings) {
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

void UIManager::handleButtons(Buttons& buttons) {
  // Pass buttons to the current screen
  // Directly forward to the active screen (must exist)
  screens[currentScreen]->handleButtons(buttons);
}

void UIManager::showSleepScreen() {
  Serial.printf("[%lu] Showing SLEEP screen\n", millis());
  
  // We need to be careful with double buffering. 
  // clearScreen affects the current back buffer.
  display.clearScreen(0xFF);

  bool usedRandomCover = false;
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
    if (coverPath.length() > 0 && SD.exists(coverPath.c_str())) {
      Serial.printf("Selecting book cover sleep screen: %s\n", coverPath.c_str());
      if (ImageDecoder::decodeToDisplayFitWidth(coverPath.c_str(), display.getBBEPAPER(), display.getFrameBuffer(), 480, 800)) {
        usedRandomCover = true;
      } else {
        Serial.println("Failed to decode book cover sleep screen");
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
        if (ImageDecoder::decodeToDisplay(selected.c_str(), display.getBBEPAPER(), display.getFrameBuffer(), 480, 800)) {
          usedRandomCover = true;
          g_lastSleepCoverIndex = idx;
          break;
        }
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
    int16_t centerX = (480 - w) / 2;

    textRenderer.setCursor(centerX, 780);
    textRenderer.print(sleepText);
  }

  // displayBuffer sends the back buffer to the controller AND swaps pointers.
  // After this call, the buffer containing the image is now the "active" front buffer.
  display.displayBuffer(EInkDisplay::FULL_REFRESH);
  
  // Note: Since displayBuffer() calls swapBuffers(), any subsequent drawing 
  // without another clearScreen/swap would happen on the old front buffer.
  // But we are entering deep sleep, so this is the final state.
  
  if (!usedRandomCover && display.supportsGrayscale()) {
    display.copyGrayscaleBuffers(bebop_image_lsb, bebop_image_msb);
    display.displayGrayBuffer(true);
  }
}

void UIManager::prepareForSleep() {
  // Notify the active screen that the device is powering down so it can
  // persist any state (e.g. current reading position).
  if (screens[currentScreen])
    screens[currentScreen]->shutdown();
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
  int16_t groupX = 480 - marginRight - groupW;
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
    if (currentScreen == ScreenId::FileBrowser) {
      screens[currentScreen]->show();
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
  // Directly access TextViewerScreen and open the file (guaranteed to exist)
  static_cast<TextViewerScreen*>(screens[ScreenId::TextViewer].get())->openFile(sdPath);
  showScreen(ScreenId::TextViewer);
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

  // Apply reading orientation only while in TextViewer; keep UI screens in portrait.
  if (id == ScreenId::TextViewer && settings) {
    int orientation = 0;
    (void)settings->getInt(String("settings.orientation"), orientation);
    switch (orientation) {
      case 0:
        textRenderer.setOrientation(TextRenderer::Portrait);
        break;
      case 1:
        textRenderer.setOrientation(TextRenderer::LandscapeClockwise);
        break;
      case 2:
        textRenderer.setOrientation(TextRenderer::PortraitInverted);
        break;
      case 3:
        textRenderer.setOrientation(TextRenderer::LandscapeCounterClockwise);
        break;
      default:
        textRenderer.setOrientation(TextRenderer::Portrait);
        break;
    }
  } else {
    textRenderer.setOrientation(TextRenderer::Portrait);
  }

  previousScreen = currentScreen;
  currentScreen = id;
  // Call activate so screens can perform any work needed when they become
  // active (this also ensures TextViewerScreen::activate is invoked to open
  // any pending file that was loaded during begin()).
  screens[id]->activate();
  screens[id]->show();
}
