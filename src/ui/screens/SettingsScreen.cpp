#include "SettingsScreen.h"

#include <resources/fonts/FontDefinitions.h>
#include <resources/fonts/FontManager.h>
#include <resources/fonts/other/MenuFontBig.h>
#include <resources/fonts/other/MenuFontSmall.h>
#include <resources/fonts/other/MenuHeader.h>

#include "../../core/BatteryMonitor.h"
#include "../../core/Buttons.h"
#include "../../core/Settings.h"
#include "../UIManager.h"

constexpr int SettingsScreen::marginValues[];
constexpr int SettingsScreen::lineHeightValues[];
constexpr int SettingsScreen::paragraphSpacingValues[];

SettingsScreen::SettingsScreen(EInkDisplay& display, TextRenderer& renderer, UIManager& uiManager)
    : display(display), textRenderer(renderer), uiManager(uiManager) {}

void SettingsScreen::begin() {
  loadSettings();
}

void SettingsScreen::handleButtons(Buttons& buttons) {
  if (buttons.isPressed(Buttons::BACK)) {
    saveSettings();
    // Return to the screen we came from
    uiManager.showScreen(uiManager.getSettingsReturnScreen());
  } else if (buttons.isPressed(Buttons::LEFT)) {
    selectNext();
  } else if (buttons.isPressed(Buttons::RIGHT)) {
    selectPrev();
  } else if (buttons.isPressed(Buttons::CONFIRM)) {
    toggleCurrentSetting();
  }
}

void SettingsScreen::activate() {
  selectedIndex = 0;
  loadSettings();
}

void SettingsScreen::show() {
  renderSettings();
  display.displayBuffer(EInkDisplay::FAST_REFRESH);
}

void SettingsScreen::renderSettings() {
  display.clearScreen(0xFF);
  textRenderer.setTextColor(TextRenderer::COLOR_BLACK);
  textRenderer.setFont(getTitleFont());

  // Set framebuffer to BW buffer for rendering
  textRenderer.setFrameBuffer(display.getFrameBuffer());
  textRenderer.setBitmapType(TextRenderer::BITMAP_BW);

  uiManager.renderStatusHeader(textRenderer);

  textRenderer.setFont(getTitleFont());

  const int16_t pageW = (int16_t)EInkDisplay::DISPLAY_WIDTH;
  const int16_t pageH = (int16_t)EInkDisplay::DISPLAY_HEIGHT;

  // Center the title horizontally
  {
    const char* title = "Settings";
    int16_t x1, y1;
    uint16_t w, h;
    textRenderer.getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
    int16_t centerX = (pageW - (int)w) / 2;
    textRenderer.setCursor(centerX, 75);
    textRenderer.print(title);
  }

  textRenderer.setFont(getMainFont());

  // Render settings list
  const int lineHeight = 28;
  int totalHeight = SETTINGS_COUNT * lineHeight;
  int startY = (pageH - totalHeight) / 2;

  for (int i = 0; i < SETTINGS_COUNT; ++i) {
    String displayName = getSettingName(i);
    displayName += ": ";
    displayName += getSettingValue(i);

    if (i == selectedIndex) {
      displayName = String(">") + displayName + String("<");
    }

    int16_t x1, y1;
    uint16_t w, h;
    textRenderer.getTextBounds(displayName.c_str(), 0, 0, &x1, &y1, &w, &h);
    int16_t centerX = (pageW - (int)w) / 2;
    int16_t rowY = startY + i * lineHeight;
    textRenderer.setCursor(centerX, rowY);
    textRenderer.print(displayName);
  }
}

void SettingsScreen::selectNext() {
  selectedIndex++;
  if (selectedIndex >= SETTINGS_COUNT)
    selectedIndex = 0;
  show();
}

void SettingsScreen::selectPrev() {
  selectedIndex--;
  if (selectedIndex < 0)
    selectedIndex = SETTINGS_COUNT - 1;
  show();
}

void SettingsScreen::toggleCurrentSetting() {
  switch (selectedIndex) {
    case 0:  // TOC
      saveSettings();
      uiManager.showScreen(UIManager::ScreenId::Chapters);
      return;
      break;
    case 1:  // Horizontal Margins
      marginIndex++;
      if (marginIndex >= marginValuesCount)
        marginIndex = 0;
      break;
    case 2:  // Line Height
      lineHeightIndex++;
      if (lineHeightIndex >= lineHeightValuesCount)
        lineHeightIndex = 0;
      break;
    case 3:  // Paragraph Spacing
      paragraphSpacingIndex++;
      if (paragraphSpacingIndex >= paragraphSpacingValuesCount)
        paragraphSpacingIndex = 0;
      break;
    case 4:  // Alignment
      alignmentIndex++;
      if (alignmentIndex >= 3)
        alignmentIndex = 0;
      break;
    case 5:  // Show Chapter Numbers
      showChapterNumbersIndex = 1 - showChapterNumbersIndex;
      break;
    case 6:  // Font Family
      fontFamilyIndex++;
      if (fontFamilyIndex >= 2)
        fontFamilyIndex = 0;
      applyFontSettings();
      break;
    case 7:  // Font Size
      fontSizeIndex++;
      if (fontSizeIndex >= 3)
        fontSizeIndex = 0;
      applyFontSettings();
      break;
    case 8:  // UI Font Size
      uiFontSizeIndex = 1 - uiFontSizeIndex;
      applyUIFontSettings();
      break;
    case 9:  // Sleep Screen
      sleepScreenModeIndex = 1 - sleepScreenModeIndex;
      break;
    case 10:  // Orientation
      orientationIndex++;
      if (orientationIndex >= 4)
        orientationIndex = 0;
      break;
    case 11:  // Time to Sleep
      sleepTimeoutIndex++;
      if (sleepTimeoutIndex >= 5)
        sleepTimeoutIndex = 0;
      break;
    case 12:  // Clock
      saveSettings();
      uiManager.showScreen(UIManager::ScreenId::ClockSettings);
      return;
      break;
    case 13:  // WiFi Setup
      saveSettings();
      uiManager.showScreen(UIManager::ScreenId::WifiSettings);
      return;
      break;
    case 14:  // Clear Cache
      clearCacheStatus = uiManager.clearEpubCache() ? 1 : 0;
      break;
    case 15:  // Startup
      startupBehaviorIndex = 1 - startupBehaviorIndex;
      break;
  }
  saveSettings();
  show();
}

void SettingsScreen::loadSettings() {
  Settings& s = uiManager.getSettings();

  // Load horizontal margins (applies to both left and right)
  int margin = 10;
  if (s.getInt(String("settings.margin"), margin)) {
    for (int i = 0; i < marginValuesCount; i++) {
      if (marginValues[i] == margin) {
        marginIndex = i;
        break;
      }
    }
  }

  // Load line height
  int lineHeight = 30;
  if (s.getInt(String("settings.lineHeight"), lineHeight)) {
    for (int i = 0; i < lineHeightValuesCount; i++) {
      if (lineHeightValues[i] == lineHeight) {
        lineHeightIndex = i;
        break;
      }
    }
  }

  // Load paragraph spacing
  int paragraphSpacing = 12;
  if (s.getInt(String("settings.paragraphSpacing"), paragraphSpacing)) {
    for (int i = 0; i < paragraphSpacingValuesCount; i++) {
      if (paragraphSpacingValues[i] == paragraphSpacing) {
        paragraphSpacingIndex = i;
        break;
      }
    }
  }

  // Load alignment
  int alignment = 0;
  if (s.getInt(String("settings.alignment"), alignment)) {
    alignmentIndex = alignment;
  }

  // Load show chapter numbers
  int showChapters = 1;
  if (s.getInt(String("settings.showChapterNumbers"), showChapters)) {
    showChapterNumbersIndex = showChapters;
  }

  // Load font family (0=NotoSans, 1=Bookerly)
  int fontFamily = 1;
  if (s.getInt(String("settings.fontFamily"), fontFamily)) {
    fontFamilyIndex = fontFamily;
  }

  // Load font size (0=Small, 1=Medium, 2=Large)
  int fontSize = 0;
  if (s.getInt(String("settings.fontSize"), fontSize)) {
    fontSizeIndex = fontSize;
  }

  // Load UI font size (0=Small/14, 1=Large/28)
  int uiFontSize = 0;
  if (s.getInt(String("settings.uiFontSize"), uiFontSize)) {
    uiFontSizeIndex = uiFontSize;
  }

  // Load sleep screen mode (0=Book Cover, 1=SD Random)
  // Migrate legacy settings.randomSleepCover (0=OFF,1=ON) to mode (OFF->Book Cover, ON->SD Random)
  int sleepMode = 0;
  if (s.getInt(String("settings.sleepScreenMode"), sleepMode)) {
    sleepScreenModeIndex = sleepMode;
  } else {
    int randomSleepCover = 0;
    if (s.getInt(String("settings.randomSleepCover"), randomSleepCover)) {
      sleepScreenModeIndex = (randomSleepCover != 0) ? 1 : 0;
    }
  }

  // Load reading orientation (0=Portrait, 1=Landscape CW, 2=Inverted, 3=Landscape CCW)
  int orientation = 0;
  if (s.getInt(String("settings.orientation"), orientation)) {
    orientationIndex = orientation;
  }

  // Load time to sleep (0=1 min, 1=5 min, 2=10 min, 3=15 min, 4=30 min)
  int sleepTimeout = 2;
  if (s.getInt(String("settings.sleepTimeout"), sleepTimeout)) {
    sleepTimeoutIndex = sleepTimeout;
  }

  // Startup behavior: 0=Home, 1=Resume last
  int startupBehavior = 1;
  if (s.getInt(String("settings.startupBehavior"), startupBehavior)) {
    startupBehaviorIndex = startupBehavior ? 1 : 0;
  }

  // Apply the loaded font settings
  applyFontSettings();
  applyUIFontSettings();
}

void SettingsScreen::saveSettings() {
  Settings& s = uiManager.getSettings();

  s.setInt(String("settings.margin"), marginValues[marginIndex]);
  s.setInt(String("settings.lineHeight"), lineHeightValues[lineHeightIndex]);
  s.setInt(String("settings.paragraphSpacing"), paragraphSpacingValues[paragraphSpacingIndex]);
  s.setInt(String("settings.alignment"), alignmentIndex);
  s.setInt(String("settings.showChapterNumbers"), showChapterNumbersIndex);
  s.setInt(String("settings.fontFamily"), fontFamilyIndex);
  s.setInt(String("settings.fontSize"), fontSizeIndex);
  s.setInt(String("settings.uiFontSize"), uiFontSizeIndex);
  s.setInt(String("settings.sleepScreenMode"), sleepScreenModeIndex);
  s.setInt(String("settings.orientation"), orientationIndex);
  s.setInt(String("settings.sleepTimeout"), sleepTimeoutIndex);
  s.setInt(String("settings.startupBehavior"), startupBehaviorIndex);

  if (!s.save()) {
    Serial.println("SettingsScreen: Failed to write settings.cfg");
  }
}

String SettingsScreen::getSettingName(int index) {
  switch (index) {
    case 0:
      return "TOC";
    case 1:
      return "Margins";
    case 2:
      return "Line Height";
    case 3:
      return "Paragraph Space";
    case 4:
      return "Alignment";
    case 5:
      return "Chapter Numbers";
    case 6:
      return "Font Family";
    case 7:
      return "Font Size";
    case 8:
      return "UI Font Size";
    case 9:
      return "Sleep Screen";
    case 10:
      return "Orientation";
    case 11:
      return "Time to Sleep";
    case 12:
      return "Clock";
    case 13:
      return "WiFi";
    case 14:
      return "Clear Cache";
    case 15:
      return "Startup";
    default:
      return "";
  }
}

String SettingsScreen::getSettingValue(int index) {
  switch (index) {
    case 0:
      return "Open";
    case 1:
      return String(marginValues[marginIndex]);
    case 2:
      return String(lineHeightValues[lineHeightIndex]);
    case 3:
      return String(paragraphSpacingValues[paragraphSpacingIndex]);
    case 4:
      switch (alignmentIndex) {
        case 0:
          return "Left";
        case 1:
          return "Center";
        case 2:
          return "Right";
        default:
          return "Unknown";
      }
    case 5:
      return showChapterNumbersIndex ? "On" : "Off";
    case 6:
      switch (fontFamilyIndex) {
        case 0:
          return "NotoSans";
        case 1:
          return "Bookerly";
        default:
          return "Unknown";
      }
    case 7:
      switch (fontSizeIndex) {
        case 0:
          return "Small";
        case 1:
          return "Medium";
        case 2:
          return "Large";
        default:
          return "Unknown";
      }
    case 8:
      return uiFontSizeIndex ? "Large" : "Small";
    case 9:
      return sleepScreenModeIndex ? "SD Random" : "Book Cover";
    case 10:
      switch (orientationIndex) {
        case 0:
          return "Portrait";
        case 1:
          return "Land CW";
        case 2:
          return "Inverted";
        case 3:
          return "Land CCW";
        default:
          return "Portrait";
      }
    case 11:
      switch (sleepTimeoutIndex) {
        case 0:
          return "1 min";
        case 1:
          return "5 min";
        case 2:
          return "10 min";
        case 3:
          return "15 min";
        case 4:
          return "30 min";
        default:
          return "10 min";
      }
    case 12:
      return "Setup";
    case 13:
      return "Setup";
    case 14:
      if (clearCacheStatus < 0)
        return "Press";
      return clearCacheStatus ? "OK" : "FAIL";
    case 15:
      return startupBehaviorIndex ? "Resume" : "Home";
    default:
      return "";
  }
}

void SettingsScreen::applyFontSettings() {
  // Determine which font family to use based on settings
  FontFamily* targetFamily = nullptr;

  if (fontFamilyIndex == 0) {  // NotoSans
    switch (fontSizeIndex) {
      case 0:
        targetFamily = &notoSans26Family;
        break;
      case 1:
        targetFamily = &notoSans28Family;
        break;
      case 2:
        targetFamily = &notoSans30Family;
        break;
    }
  } else if (fontFamilyIndex == 1) {  // Bookerly
    switch (fontSizeIndex) {
      case 0:
        targetFamily = &bookerly26Family;
        break;
      case 1:
        targetFamily = &bookerly28Family;
        break;
      case 2:
        targetFamily = &bookerly30Family;
        break;
    }
  }

  if (targetFamily) {
    setCurrentFontFamily(targetFamily);
  }
}

void SettingsScreen::applyUIFontSettings() {
  // Set main and title fonts for UI elements
  // Always use MenuHeader for headers/titles
  setTitleFont(&MenuHeader);

  if (uiFontSizeIndex == 0) {
    setMainFont(&MenuFontSmall);
  } else {
    setMainFont(&MenuFontBig);
  }
}
