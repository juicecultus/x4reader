#include "WifiPasswordEntryScreen.h"

#include <resources/fonts/FontManager.h>
#include <resources/fonts/other/MenuFontSmall.h>
#include <resources/fonts/other/MenuHeader.h>

#include "../../core/Settings.h"
#include "../UIManager.h"

static const char* kPwChars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.@";
static const int kChoiceOk = 0;
static const int kChoiceDel = 1;
static const int kChoiceCharsStart = 2;

WifiPasswordEntryScreen::WifiPasswordEntryScreen(EInkDisplay& display, TextRenderer& renderer, UIManager& uiManager)
    : display(display), textRenderer(renderer), uiManager(uiManager) {}

void WifiPasswordEntryScreen::begin() {
  loadSettings();
}

void WifiPasswordEntryScreen::activate() {
  loadSettings();
  editOriginal = wifiPass;
  editBuffer = wifiPass;
  choiceIndex = kChoiceCharsStart;
}

void WifiPasswordEntryScreen::handleButtons(Buttons& buttons) {
  int choicesLen = kChoiceCharsStart + (int)strlen(kPwChars);

  if (buttons.isPressed(Buttons::BACK)) {
    editBuffer = editOriginal;
    uiManager.showScreen(UIManager::ScreenId::WifiSettings);
  } else if (buttons.isPressed(Buttons::LEFT)) {
    choiceIndex++;
    if (choiceIndex >= choicesLen)
      choiceIndex = 0;
    show();
  } else if (buttons.isPressed(Buttons::RIGHT)) {
    choiceIndex--;
    if (choiceIndex < 0)
      choiceIndex = choicesLen - 1;
    show();
  } else if (buttons.isPressed(Buttons::CONFIRM)) {
    choose();
  }
}

void WifiPasswordEntryScreen::show() {
  render();
  display.displayBuffer(EInkDisplay::FAST_REFRESH);
}

void WifiPasswordEntryScreen::loadSettings() {
  Settings& s = uiManager.getSettings();
  wifiPass = s.getString(String("wifi.pass"));
}

void WifiPasswordEntryScreen::saveSettings() {
  Settings& s = uiManager.getSettings();
  s.setString(String("wifi.pass"), wifiPass);
  if (!s.save()) {
    Serial.println("WifiPasswordEntryScreen: Failed to write settings.cfg");
  }
}

void WifiPasswordEntryScreen::choose() {
  if (choiceIndex == kChoiceOk) {
    wifiPass = editBuffer;
    saveSettings();
    uiManager.showScreen(UIManager::ScreenId::WifiSettings);
    return;
  }
  if (choiceIndex == kChoiceDel) {
    if (editBuffer.length() > 0) {
      editBuffer.remove(editBuffer.length() - 1);
    }
    show();
    return;
  }

  int charIdx = choiceIndex - kChoiceCharsStart;
  if (charIdx >= 0 && charIdx < (int)strlen(kPwChars)) {
    char c = kPwChars[charIdx];
    if (editBuffer.length() < 64) {
      editBuffer += c;
    }
  }
  show();
}

void WifiPasswordEntryScreen::render() {
  display.clearScreen(0xFF);
  textRenderer.setTextColor(TextRenderer::COLOR_BLACK);
  textRenderer.setFont(getTitleFont());

  textRenderer.setFrameBuffer(display.getFrameBuffer());
  textRenderer.setBitmapType(TextRenderer::BITMAP_BW);

  {
    const char* title = "WiFi Password";
    int16_t x1, y1;
    uint16_t w, h;
    textRenderer.getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
    int16_t centerX = (480 - (int)w) / 2;
    textRenderer.setCursor(centerX, 75);
    textRenderer.print(title);
  }

  textRenderer.setFont(getMainFont());

  {
    String shown;
    for (int i = 0; i < editBuffer.length() && i < 32; ++i)
      shown += "*";
    if (editBuffer.length() > 32)
      shown += "...";

    String line = String("Password: ") + shown;
    int16_t x1, y1;
    uint16_t w, h;
    textRenderer.getTextBounds(line.c_str(), 0, 0, &x1, &y1, &w, &h);
    int16_t centerX = (480 - (int)w) / 2;
    textRenderer.setCursor(centerX, 200);
    textRenderer.print(line);
  }

  {
    String choice;
    if (choiceIndex == kChoiceOk) {
      choice = "[OK]";
    } else if (choiceIndex == kChoiceDel) {
      choice = "[DEL]";
    } else {
      int charIdx = choiceIndex - kChoiceCharsStart;
      char c = (charIdx >= 0 && charIdx < (int)strlen(kPwChars)) ? kPwChars[charIdx] : '?';
      choice = String("[") + String(c) + String("]");
    }

    int16_t x1, y1;
    uint16_t w, h;
    textRenderer.getTextBounds(choice.c_str(), 0, 0, &x1, &y1, &w, &h);
    int16_t centerX = (480 - (int)w) / 2;
    textRenderer.setCursor(centerX, 260);
    textRenderer.print(choice);
  }

  {
    textRenderer.setFont(&MenuFontSmall);
    textRenderer.setCursor(20, 780);
    textRenderer.print("Left/Right: Choose  OK: Select  Back: Cancel");
  }
}
