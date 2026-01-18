#include "ChaptersScreen.h"

#include <resources/fonts/FontManager.h>

#include "../../core/Buttons.h"
#include "../UIManager.h"
#include "TextViewerScreen.h"

ChaptersScreen::ChaptersScreen(EInkDisplay& display, TextRenderer& renderer, UIManager& uiManager)
    : display(display), textRenderer(renderer), uiManager(uiManager) {}

void ChaptersScreen::activate() {
  selectedIndex = 0;
}

void ChaptersScreen::handleButtons(Buttons& buttons) {
  if (buttons.isPressed(Buttons::BACK)) {
    uiManager.showScreen(UIManager::ScreenId::Settings);
  } else if (buttons.isPressed(Buttons::LEFT)) {
    selectNext();
  } else if (buttons.isPressed(Buttons::RIGHT)) {
    selectPrev();
  } else if (buttons.isPressed(Buttons::CONFIRM)) {
    confirm();
  }
}

void ChaptersScreen::show() {
  render();
  display.displayBuffer(EInkDisplay::FAST_REFRESH);
}

int ChaptersScreen::getChapterCount() const {
  Screen* s = uiManager.getScreen(UIManager::ScreenId::TextViewer);
  TextViewerScreen* tv = static_cast<TextViewerScreen*>(s);
  if (!tv)
    return 0;
  return tv->getChapterCount();
}

String ChaptersScreen::getChapterLabel(int index) const {
  Screen* s = uiManager.getScreen(UIManager::ScreenId::TextViewer);
  TextViewerScreen* tv = static_cast<TextViewerScreen*>(s);
  if (!tv)
    return String("");

  String name = tv->getChapterName(index);
  if (name.length() == 0) {
    return String("Chapter ") + String(index + 1);
  }

  if (name.length() > 30) {
    name = name.substring(0, 27) + "...";
  }
  return name;
}

void ChaptersScreen::render() {
  display.clearScreen(0xFF);
  textRenderer.setTextColor(TextRenderer::COLOR_BLACK);
  textRenderer.setFont(getTitleFont());

  textRenderer.setFrameBuffer(display.getFrameBuffer());
  textRenderer.setBitmapType(TextRenderer::BITMAP_BW);

  uiManager.renderStatusHeader(textRenderer);

  textRenderer.setFont(getTitleFont());

  const int16_t pageW = (int16_t)EInkDisplay::DISPLAY_WIDTH;
  const int16_t pageH = (int16_t)EInkDisplay::DISPLAY_HEIGHT;

  {
    const char* title = "Chapters";
    int16_t x1, y1;
    uint16_t w, h;
    textRenderer.getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
    int16_t centerX = (pageW - (int)w) / 2;
    textRenderer.setCursor(centerX, 75);
    textRenderer.print(title);
  }

  textRenderer.setFont(getMainFont());

  int count = getChapterCount();
  if (count <= 0) {
    return;
  }

  const int lineHeight = 28;
  int maxLines = 16;
  if (count < maxLines)
    maxLines = count;

  int startIndex = 0;
  if (selectedIndex >= maxLines) {
    startIndex = selectedIndex - (maxLines - 1);
  }

  int totalHeight = maxLines * lineHeight;
  int startY = (pageH - totalHeight) / 2;

  for (int i = 0; i < maxLines; ++i) {
    int idx = startIndex + i;
    String line = getChapterLabel(idx);
    if (idx == selectedIndex) {
      line = String(">") + line + String("<");
    }

    int16_t x1, y1;
    uint16_t w, h;
    textRenderer.getTextBounds(line.c_str(), 0, 0, &x1, &y1, &w, &h);
    int16_t centerX = (pageW - (int)w) / 2;
    int16_t rowY = startY + i * lineHeight;
    textRenderer.setCursor(centerX, rowY);
    textRenderer.print(line);
  }
}

void ChaptersScreen::selectNext() {
  int count = getChapterCount();
  if (count <= 0)
    return;
  selectedIndex++;
  if (selectedIndex >= count)
    selectedIndex = 0;
  show();
}

void ChaptersScreen::selectPrev() {
  int count = getChapterCount();
  if (count <= 0)
    return;
  selectedIndex--;
  if (selectedIndex < 0)
    selectedIndex = count - 1;
  show();
}

void ChaptersScreen::confirm() {
  Screen* s = uiManager.getScreen(UIManager::ScreenId::TextViewer);
  TextViewerScreen* tv = static_cast<TextViewerScreen*>(s);
  if (!tv)
    return;

  tv->goToChapterStart(selectedIndex);
  uiManager.showScreen(UIManager::ScreenId::TextViewer);
}
