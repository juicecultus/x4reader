#ifndef IMAGE_VIEWER_SCREEN_H
#define IMAGE_VIEWER_SCREEN_H

#include "../../core/EInkDisplay.h"
#include "../UIManager.h"
#include "Screen.h"

class ImageViewerScreen : public Screen {
 public:
  ImageViewerScreen(EInkDisplay& display, UIManager& uiManager);

  void handleButtons(class Buttons& buttons) override;
  void show() override;

 private:
  EInkDisplay& display;
  UIManager& uiManager;

  int index = 0;
};

#endif
