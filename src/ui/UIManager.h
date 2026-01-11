#ifndef UI_MANAGER_H
#define UI_MANAGER_H

#include <Arduino.h>
#include <map>
#include <memory>

#include "core/Buttons.h"
#include "core/EInkDisplay.h"
#include "rendering/TextRenderer.h"
#include "text/layout/LayoutStrategy.h"
#include "ui/screens/Screen.h"

class SDCardManager;

// Forward-declare concrete screen types (global, not nested)
class FileBrowserScreen;
class ImageViewerScreen;
class TextViewerScreen;
class XtcViewerScreen;
class SettingsScreen;
class ChaptersScreen;
class WifiSettingsScreen;
class WifiSsidSelectScreen;
class WifiPasswordEntryScreen;
class ClockSettingsScreen;
class TimezoneSelectScreen;

class Settings;

class UIManager {
 public:
  // Typed screen identifiers so callers don't use raw indices
  enum class ScreenId {
    FileBrowser,
    ImageViewer,
    TextViewer,
    XtcViewer,
    Settings,
    Chapters,
    ClockSettings,
    WifiSettings,
    WifiSsidSelect,
    WifiPasswordEntry,
    TimezoneSelect,
    Count
  };

  // Constructor
  UIManager(EInkDisplay& display, class SDCardManager& sdManager);
  ~UIManager();

  void begin();
  void handleButtons(Buttons& buttons);
  void showSleepScreen();
  // Prepare UI for power-off: notify active screen to persist state
  void prepareForSleep();

  // Show a screen by id
  void showScreen(ScreenId id);

  // Open a text file (path on SD) in the text viewer and switch to that screen.
  void openTextFile(const String& sdPath);

  // Open an XTC/XTCH file in the XTC viewer and switch to that screen.
  void openXtcFile(const String& sdPath);

  bool clearEpubCache();

  void setClockHM(int hour, int minute);
  bool getClockHM(int& hourOut, int& minuteOut);
  String getClockString();

  void renderStatusHeader(TextRenderer& renderer);

  void trySyncTimeFromNtp();

 private:
  static void ntpSyncTaskTrampoline(void* param);
  void startAutoNtpSyncIfEnabled();

  EInkDisplay& display;
  SDCardManager& sdManager;
  TextRenderer textRenderer;

  bool clockValid = false;
  int32_t clockBaseMinutes = 0;
  uint32_t clockBaseMillis = 0;

  bool ntpTimeValid = false;

  bool ntpSyncInProgress = false;
  TaskHandle_t ntpSyncTaskHandle = nullptr;

  ScreenId currentScreen = ScreenId::FileBrowser;
  ScreenId previousScreen = ScreenId::FileBrowser;
  ScreenId settingsReturnScreen = ScreenId::FileBrowser;

  // Map holding owning pointers to the screens; screens are
  // constructed in the .cpp ctor and live for the UIManager lifetime.
  std::map<ScreenId, std::unique_ptr<Screen>> screens;

  // Global settings manager (single consolidated settings file)
  class Settings* settings = nullptr;

 public:
  Settings& getSettings() {
    return *settings;
  }

  Screen* getScreen(ScreenId id) {
    auto it = screens.find(id);
    if (it != screens.end()) {
      return it->second.get();
    }
    return nullptr;
  }

  ScreenId getPreviousScreen() const {
    return previousScreen;
  }

  ScreenId getSettingsReturnScreen() const {
    return settingsReturnScreen;
  }
};

#endif
