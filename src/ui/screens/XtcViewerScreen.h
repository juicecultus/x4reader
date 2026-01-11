#pragma once

#include <Arduino.h>

#include "../../core/EInkDisplay.h"
#include "../../core/SDCardManager.h"
#include "../../rendering/TextRenderer.h"
#include "resources/fonts/FontManager.h"
#include "../UIManager.h"
#include "Screen.h"
#include "../../content/xtc/XtcFile.h"

class XtcViewerScreen : public Screen {
 public:
  XtcViewerScreen(EInkDisplay& display, TextRenderer& renderer, SDCardManager& sdManager, UIManager& uiManager);

  void begin() override;
  void activate() override;
  void show() override;
  void handleButtons(Buttons& buttons) override;
  void shutdown() override;

  void openFile(const String& sdPath);
  void closeDocument();

 private:
  EInkDisplay& display;
  TextRenderer& textRenderer;
  SDCardManager& sdManager;
  UIManager& uiManager;

  XtcFile xtc;
  bool valid = false;
  uint32_t currentPage = 0;

  String pendingOpenPath;
  String currentFilePath;

  void renderPage();
  void nextPage();
  void prevPage();

  void saveSettingsToFile();
  void loadSettingsFromFile();
  void savePositionToFile();
  void loadPositionFromFile();
};
