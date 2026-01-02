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
  static constexpr int SETTINGS_COUNT = 4;

  // Setting values and their current indices
  int marginIndex = 1;
  int lineHeightIndex = 1;
  int alignmentIndex = 0;
  int showChapterNumbersIndex = 0;

  // Available values for each setting
  static constexpr int marginValues[] = {5, 10, 15, 20, 25, 30};
  static constexpr int marginValuesCount = 6;
  static constexpr int lineHeightValues[] = {0, 2, 4, 6, 8, 10};
  static constexpr int lineHeightValuesCount = 6;
  // Alignment: 0=LEFT, 1=CENTER, 2=RIGHT
  // showChapterNumbers: 0=OFF, 1=ON

  void renderSettings();
  void selectNext();
  void selectPrev();
  void toggleCurrentSetting();
  void loadSettings();
  void saveSettings();
  String getSettingName(int index);
  String getSettingValue(int index);
};

#endif
