#ifndef WIFI_PASSWORD_ENTRY_SCREEN_H
#define WIFI_PASSWORD_ENTRY_SCREEN_H

#include <Arduino.h>

#include "../../core/EInkDisplay.h"
#include "../../rendering/TextRenderer.h"
#include "Screen.h"

class Buttons;
class UIManager;

class WifiPasswordEntryScreen : public Screen {
 public:
  WifiPasswordEntryScreen(EInkDisplay& display, TextRenderer& renderer, UIManager& uiManager);

  void begin() override;
  void handleButtons(Buttons& buttons) override;
  void activate() override;
  void show() override;
  void shutdown() override {}

 private:
  EInkDisplay& display;
  TextRenderer& textRenderer;
  UIManager& uiManager;

  String wifiPass;
  String editOriginal;
  String editBuffer;
  int keyRow = 0;
  int keyCol = 0;
  bool caps = false;
  bool symbols = false;
  
  // Touch tracking
  int16_t lastTouchX = -1;
  int16_t lastTouchY = -1;
  bool touchPressed = false;
  int pressedRow = -1;
  int pressedCol = -1;

  void loadSettings();
  void saveSettings();
  void render();
  void renderKey(char key, const char* label, int x, int y, int w, int h, bool selected);

  void chooseKey();
  void chooseKeyAt(int row, int col);
  void chooseKeyCode(char key);
  bool hitTestKey(int16_t touchX, int16_t touchY, int& outRow, int& outCol);
  bool getKeyRect(int row, int col, int& x, int& y, int& w, int& h, char& keyOut, const char*& labelOut);
  
  // Keyboard layout constants for 540x960 display
  static constexpr int kKeyboardStartY = 340;
  static constexpr int kKeyHeight = 64;
  static constexpr int kKeySpacing = 6;
  static constexpr int kKeyboardRows = 5;
};

#endif
