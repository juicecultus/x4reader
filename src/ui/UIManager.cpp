#include "UIManager.h"

#include <Arduino.h>
#include <resources/fonts/FontManager.h>

#include "core/ImageDecoder.h"
#include "core/Settings.h"
#include "resources/images/bebop_image.h"
#include "ui/screens/FileBrowserScreen.h"
#include "ui/screens/ImageViewerScreen.h"
#include "ui/screens/SettingsScreen.h"
#include "ui/screens/TextViewerScreen.h"

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
  // Initialize screens using generic Screen interface
  for (auto const& [id, p] : screens) {
    if (p)
      p->begin();
  }

  // Restore last-visible screen (use consolidated settings when available)
  currentScreen = ScreenId::FileBrowser;
  ScreenId savedPreviousScreen = ScreenId::FileBrowser;

  if (sdManager.ready() && settings) {
    int saved = 0;
    if (settings->getInt(String("ui.screen"), saved)) {
      if (saved >= 0 && saved <= static_cast<int>(ScreenId::Settings)) {
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
      if (prevSaved >= 0 && prevSaved <= static_cast<int>(ScreenId::Settings)) {
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
  display.clearScreen(0xFF);

  bool usedRandomCover = false;
  int randomSleepCover = 0;
  
    if (settings && settings->getInt(String("settings.randomSleepCover"), randomSleepCover) && randomSleepCover != 0) {
    auto files = sdManager.listFiles("/images", 50);
    std::vector<String> images;
    for (const auto& f : files) {
      String lf = f;
      lf.toLowerCase();
      // Filter out macOS metadata files which might be corrupted or invalid
      if (lf.startsWith("._")) continue;
      if (lf.endsWith(".jpg") || lf.endsWith(".jpeg") || lf.endsWith(".png")) {
        images.push_back(f);
      }
    }

    if (!images.empty()) {
      // Simple random selection
      srand(millis());
      int idx = rand() % images.size();
      String selected = String("/images/") + images[idx];
      Serial.printf("Selecting random sleep cover: %s\n", selected.c_str());
      
      // Use BBEPAPER driver instance directly via EInkDisplay accessor
      if (ImageDecoder::decodeToDisplay(selected.c_str(), display.getBBEPAPER(), display.getFrameBuffer(), EInkDisplay::DISPLAY_WIDTH, EInkDisplay::DISPLAY_HEIGHT)) {
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

  // Add "Sleeping..." text at the bottom
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

  // Final refresh to E-Ink hardware
  display.displayBuffer(EInkDisplay::FULL_REFRESH);
  
  // Re-copy back to ensure we have the image in the current front buffer
  // after the internal swap. This ensures subsequent drawings (like Sleeping...)
  // and the final display state are consistent.
  // Actually, Sleeping... was already drawn into the buffer that just became the back buffer.
  // Let's simplify: bypass the grayscale part for now if using random cover.
  
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
    if (!settings->save()) {
      Serial.println("UIManager: Failed to write settings.cfg to SD");
    }
  } else {
    Serial.println("UIManager: SD not ready; skipping save of current screen");
  }
}

void UIManager::openTextFile(const String& sdPath) {
  Serial.printf("UIManager: openTextFile %s\n", sdPath.c_str());
  // Directly access TextViewerScreen and open the file (guaranteed to exist)
  static_cast<TextViewerScreen*>(screens[ScreenId::TextViewer].get())->openFile(sdPath);
  showScreen(ScreenId::TextViewer);
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
