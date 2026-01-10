#include "UIManager.h"

#include <Arduino.h>
#include <resources/fonts/FontManager.h>

#include <WiFi.h>
#include <time.h>

#include <esp_wifi.h>

#include <esp_system.h>

#include "core/ImageDecoder.h"
#include "core/Settings.h"
#include "resources/images/bebop_image.h"
#include "ui/screens/FileBrowserScreen.h"
#include "ui/screens/ImageViewerScreen.h"
#include "ui/screens/SettingsScreen.h"
#include "ui/screens/TextViewerScreen.h"
#include "ui/screens/WifiPasswordEntryScreen.h"
#include "ui/screens/WifiSettingsScreen.h"
#include "ui/screens/WifiSsidSelectScreen.h"

RTC_DATA_ATTR static int32_t g_lastSleepCoverIndex = -1;

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
  screens[ScreenId::WifiSettings] =
      std::unique_ptr<Screen>(new WifiSettingsScreen(display, textRenderer, *this));
  screens[ScreenId::WifiSsidSelect] =
      std::unique_ptr<Screen>(new WifiSsidSelectScreen(display, textRenderer, *this));
  screens[ScreenId::WifiPasswordEntry] =
      std::unique_ptr<Screen>(new WifiPasswordEntryScreen(display, textRenderer, *this));
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

  // NTP sync is triggered manually from the WiFi settings screen.

  // Restore soft clock (HH:MM) from consolidated settings
  if (sdManager.ready() && settings) {
    int h = -1;
    int m = -1;
    bool hasH = settings->getInt(String("clock.hour"), h);
    bool hasM = settings->getInt(String("clock.minute"), m);
    if (hasH && hasM) {
      Serial.printf("[%lu] UIManager: Restored clock %02d:%02d from settings\n", millis(), h, m);
      setClockHM(h, m);
    } else {
      Serial.printf("[%lu] UIManager: No clock in settings (hour=%d ok=%d, minute=%d ok=%d)\n", millis(), h, hasH ? 1 : 0,
                    m, hasM ? 1 : 0);
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

  Serial.printf("[%lu] UIManager initialized\n", millis());
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
  int randomSleepCover = 0;
  
  if (settings && settings->getInt(String("settings.randomSleepCover"), randomSleepCover) && randomSleepCover != 0) {
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
      uint32_t r = esp_random();
      int idx = (int)(r % images.size());

      // Avoid immediately repeating the same image across deep sleep cycles.
      if ((int)images.size() > 1 && idx == g_lastSleepCoverIndex) {
        idx = (idx + 1 + (int)((r >> 16) % (images.size() - 1))) % images.size();
      }
      g_lastSleepCoverIndex = idx;

      String selected = String("/images/") + images[idx];
      Serial.printf("Selecting random sleep cover: %s\n", selected.c_str());
      
      // decodeToDisplay writes directly to the buffer we pass it.
      // We pass the current back buffer (which display.getFrameBuffer() returns).
      if (ImageDecoder::decodeToDisplay(selected.c_str(), display.getBBEPAPER(), display.getFrameBuffer(), 480, 800)) {
        usedRandomCover = true;
      } else {
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
  if (ntpTimeValid) {
    time_t now = time(nullptr);
    // Treat anything before 2020-01-01 as invalid / unsynced
    if (now > 1577836800) {
      struct tm tmNow;
      localtime_r(&now, &tmNow);
      char buf[6];
      snprintf(buf, sizeof(buf), "%02d:%02d", tmNow.tm_hour, tmNow.tm_min);
      return String(buf);
    }
    ntpTimeValid = false;
  }

  int h = 0;
  int m = 0;
  if (!getClockHM(h, m)) {
    return String("--:--");
  }
  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", h, m);
  return String(buf);
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
  WiFi.setSleep(true);

  // Eero (and other mesh APs) can advertise multiple BSSIDs for the same SSID across bands.
  // On ESP32-C3 we've observed crashes in the WPA3/SAE path; to avoid this we scan and pin a
  // WPA2 BSSID on 2.4GHz (channel <= 14) when available.
  bool havePinnedBssid = false;
  uint8_t pinnedBssid[6] = {0};
  uint8_t pinnedChannel = 0;
  {
    int n = WiFi.scanNetworks(false, true);
    Serial.printf("UIManager: scanNetworks found %d\n", n);

    // Prefer WPA2 on 2.4GHz.
    for (int i = 0; i < n; ++i) {
      if (WiFi.SSID(i) != ssid)
        continue;

      int ch = WiFi.channel(i);
      wifi_auth_mode_t auth = WiFi.encryptionType(i);
      const uint8_t* bssid = WiFi.BSSID(i);

      Serial.printf("UIManager: candidate ssid='%s' ch=%d auth=%d bssid=%02X:%02X:%02X:%02X:%02X:%02X\n",
                    ssid.c_str(), ch, (int)auth,
                    bssid ? bssid[0] : 0, bssid ? bssid[1] : 0, bssid ? bssid[2] : 0,
                    bssid ? bssid[3] : 0, bssid ? bssid[4] : 0, bssid ? bssid[5] : 0);

      if (ch > 0 && ch <= 14 && auth == WIFI_AUTH_WPA2_PSK && bssid) {
        memcpy(pinnedBssid, bssid, 6);
        pinnedChannel = (uint8_t)ch;
        havePinnedBssid = true;
        break;
      }
    }

    // Fallback: any WPA2 candidate (even if channel unknown).
    if (!havePinnedBssid) {
      for (int i = 0; i < n; ++i) {
        if (WiFi.SSID(i) != ssid)
          continue;
        wifi_auth_mode_t auth = WiFi.encryptionType(i);
        const uint8_t* bssid = WiFi.BSSID(i);
        int ch = WiFi.channel(i);
        if (auth == WIFI_AUTH_WPA2_PSK && bssid) {
          memcpy(pinnedBssid, bssid, 6);
          pinnedChannel = (ch > 0 && ch <= 255) ? (uint8_t)ch : 0;
          havePinnedBssid = true;
          break;
        }
      }
    }

    WiFi.scanDelete();
  }

  if (!havePinnedBssid) {
    Serial.printf("UIManager: No WPA2 BSSID found for '%s'. This SSID may be WPA3-only/transition.\n", ssid.c_str());
    Serial.println("UIManager: On eero, try enabling Guest network and set it to WPA2 if possible.");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    return;
  }

  Serial.printf("UIManager: WiFi connecting to '%s'...\n", ssid.c_str());

  // Connect using esp-idf config to force WPA2-only and avoid WPA3/SAE crashes.
  // This also prevents PMF from being required (some APs can misbehave).
  wifi_config_t cfg;
  memset(&cfg, 0, sizeof(cfg));
  strncpy((char*)cfg.sta.ssid, ssid.c_str(), sizeof(cfg.sta.ssid) - 1);
  strncpy((char*)cfg.sta.password, pass.c_str(), sizeof(cfg.sta.password) - 1);
  cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
  cfg.sta.pmf_cfg.capable = false;
  cfg.sta.pmf_cfg.required = false;
  cfg.sta.bssid_set = 1;
  memcpy(cfg.sta.bssid, pinnedBssid, 6);
  cfg.sta.channel = pinnedChannel;

  Serial.printf("UIManager: pinning BSSID %02X:%02X:%02X:%02X:%02X:%02X ch=%d\n",
                pinnedBssid[0], pinnedBssid[1], pinnedBssid[2], pinnedBssid[3], pinnedBssid[4], pinnedBssid[5],
                (int)pinnedChannel);

  esp_err_t rc = esp_wifi_set_config(WIFI_IF_STA, &cfg);
  if (rc != ESP_OK) {
    Serial.printf("UIManager: esp_wifi_set_config failed rc=%d\n", (int)rc);
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    return;
  }
  rc = esp_wifi_connect();
  if (rc != ESP_OK) {
    Serial.printf("UIManager: esp_wifi_connect failed rc=%d\n", (int)rc);
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    return;
  }

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
  configTime((long)gmtOffset, (int)daylightOffset, "pool.ntp.org", "time.nist.gov", "time.google.com");

  struct tm tmNow;
  if (getLocalTime(&tmNow, 6000)) {
    ntpTimeValid = true;
    Serial.printf("UIManager: NTP time synced: %04d-%02d-%02d %02d:%02d:%02d\n",
                  tmNow.tm_year + 1900, tmNow.tm_mon + 1, tmNow.tm_mday, tmNow.tm_hour, tmNow.tm_min, tmNow.tm_sec);
  } else {
    Serial.println("UIManager: NTP sync failed (getLocalTime timeout)");
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
  previousScreen = currentScreen;
  currentScreen = id;
  // Call activate so screens can perform any work needed when they become
  // active (this also ensures TextViewerScreen::activate is invoked to open
  // any pending file that was loaded during begin()).
  screens[id]->activate();
  screens[id]->show();
}
