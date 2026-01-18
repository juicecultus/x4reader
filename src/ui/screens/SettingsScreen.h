#ifndef SETTINGSSCREEN_H
#define SETTINGSSCREEN_H

#include "../../core/EInkDisplay.h"
#include "../../rendering/TextRenderer.h"
#include "Screen.h"

class Buttons;
class UIManager;

class SettingsScreen : public Screen {
 public:
  SettingsScreen(EInkDisplay& display, TextRenderer& renderer, UIManager& uiManager);

  void begin() override;
  void handleButtons(Buttons& buttons) override;
  void activate() override;
  void show() override;
  void shutdown() override {}

 private:
  EInkDisplay& display;
  TextRenderer& textRenderer;
  UIManager& uiManager;

  // Menu navigation
  int selectedIndex = 0;
  static constexpr int SETTINGS_COUNT = 16;

  // Setting values and their current indices
  int marginIndex = 1;
  int lineHeightIndex = 2;
  int paragraphSpacingIndex = 2;
  int alignmentIndex = 3;
  int showChapterNumbersIndex = 4;
  int fontFamilyIndex = 5;  // 0=NotoSans, 1=Bookerly
  int fontSizeIndex = 6;    // 0=Small(26), 1=Medium(28), 2=Large(30)
  int uiFontSizeIndex = 7;  // 0=Small(14), 1=Large(28)
  // Sleep screen mode: 0=Book Cover (default), 1=SD Random
  int sleepScreenModeIndex = 0;

  // Reading orientation: 0=Portrait, 1=Landscape
  int orientationIndex = 0;

  // Time to sleep: 0=1 min, 1=5 min, 2=10 min, 3=15 min, 4=30 min
  int sleepTimeoutIndex = 2;

  // Startup behavior: 0=Home, 1=Resume last (default)
  int startupBehaviorIndex = 1;

  int clearCacheStatus = -1; // -1=idle, 0=fail, 1=ok

  // Available values for each setting
  static constexpr int marginValues[] = {5, 10, 15, 20, 25, 30};
  static constexpr int marginValuesCount = 6;
  static constexpr int lineHeightValues[] = {0, 2, 4, 6, 8, 10};
  static constexpr int lineHeightValuesCount = 6;
  static constexpr int paragraphSpacingValues[] = {0, 6, 12, 18, 24, 30};
  static constexpr int paragraphSpacingValuesCount = 6;
  // Alignment: 0=LEFT, 1=CENTER, 2=RIGHT
  // showChapterNumbers: 0=OFF, 1=ON

  void renderSettings();
  void selectNext();
  void selectPrev();
  void toggleCurrentSetting();
  void loadSettings();
  void saveSettings();
  void applyFontSettings();
  void applyUIFontSettings();
  String getSettingName(int index);
  String getSettingValue(int index);
};

#endif
