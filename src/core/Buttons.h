#ifndef BUTTONS_H
#define BUTTONS_H

#include <Arduino.h>

class Buttons {
 public:
  Buttons();
  void begin();
  uint8_t getState();
  void update();

  // Set orientation for touch zone mapping (0=Portrait, 1=Landscape)
  void setOrientation(int orientation) { _orientation = orientation; }

  // Enable/disable the default 3-zone touch navigation mapping (LEFT/RIGHT/CONFIRM/BACK zone).
  // This should be disabled for screens that need precise touch hit-testing (e.g. on-screen keyboard).
  void setZoneNavigationEnabled(bool enabled) { _zoneNavigationEnabled = enabled; }

  // Button state queries
  bool isDown(uint8_t buttonIndex);                    // Is button currently held down?
  bool isPressed(uint8_t buttonIndex);                 // Was button just pressed this frame?
  bool wasDown(uint8_t buttonIndex);                   // Was button down in previous frame?
  bool wasReleased(uint8_t buttonIndex);               // Was button just released this frame?
  unsigned long getHoldDuration(uint8_t buttonIndex);  // How long button has been held (ms)

  bool wasAnyPressed();
  bool wasAnyReleased();

  // Raw touch coordinate access (for custom touch handling like keyboards)
  // Returns true if touch is active, fills x/y with portrait coordinates
  bool getTouchPosition(int16_t& x, int16_t& y);
  bool wasTouchReleased();  // True if touch was just released this frame

  // Button indices
  static const uint8_t BACK = 0;
  static const uint8_t CONFIRM = 1;
  static const uint8_t LEFT = 2;
  static const uint8_t RIGHT = 3;
  static const uint8_t VOLUME_UP = 4;
  static const uint8_t VOLUME_DOWN = 5;
  static const uint8_t POWER = 6;

  // Power button methods
  bool isPowerButtonDown();

  // Button names
  static const char* getButtonName(uint8_t buttonIndex);

 private:
  int getButtonFromADC(int adcValue, const int thresholds[], int numButtons);

  uint8_t currentState;
  uint8_t previousState;  // State from previous update() call

  // Per-button debounce state
  static const uint8_t NUM_BUTTONS = 7;
  uint8_t lastButtonState[NUM_BUTTONS];         // Raw state from last read
  unsigned long lastDebounceTime[NUM_BUTTONS];  // Per-button debounce timers (also used for hold duration)

  static const int BUTTON_ADC_PIN_1 = 1;
  static const int NUM_BUTTONS_1 = 4;
  static const int ADC_THRESHOLDS_1[];

  static const int POWER_BUTTON_PIN = 3;

  static const int BUTTON_ADC_PIN_2 = 2;
  static const int NUM_BUTTONS_2 = 2;
  static const int ADC_THRESHOLDS_2[];

  static const int ADC_TOLERANCE = 400;
  static const int ADC_NO_BUTTON = 3800;
  static const unsigned long DEBOUNCE_DELAY = 5;

  static const char* BUTTON_NAMES[];

  // Current orientation for touch zone mapping (0=Portrait, 1=Landscape)
  int _orientation = 0;

  bool _zoneNavigationEnabled = true;

  // Touch state tracking
  bool _touchActive = false;
  bool _prevTouchActive = false;
  int16_t _touchX = 0;
  int16_t _touchY = 0;
  int _touchCount = 0;
};

#endif
