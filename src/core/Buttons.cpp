#include "Buttons.h"

#ifdef USE_M5UNIFIED
#include <bb_captouch.h>

#include "EInkDisplay.h"

static BBCapTouch g_bbct;
static bool g_bbctInited = false;

static constexpr int TOUCH_SDA = 41;
static constexpr int TOUCH_SCL = 42;
static constexpr int TOUCH_INT = 48;
static constexpr int TOUCH_RST = -1;
#endif

const int Buttons::ADC_THRESHOLDS_1[] = {3470, 2655, 1470, 3};
const int Buttons::ADC_THRESHOLDS_2[] = {2205, 3};
const char* Buttons::BUTTON_NAMES[] = {"Back", "Confirm", "Left", "Right", "Volume Up", "Volume Down", "Power"};

Buttons::Buttons() : currentState(0), previousState(0) {
  // Initialize per-button debounce state
  for (uint8_t i = 0; i < NUM_BUTTONS; i++) {
    lastButtonState[i] = 0;
    lastDebounceTime[i] = 0;
  }
}

void Buttons::begin() {
#ifdef USE_M5UNIFIED
  if (!g_bbctInited) {
    const int rc = g_bbct.init(TOUCH_SDA, TOUCH_SCL, TOUCH_RST, TOUCH_INT);
    Serial.printf("Buttons: BBCapTouch init rc=%d\n", rc);
    if (rc == CT_SUCCESS) {
      Serial.printf("Buttons: Touch sensor type=%d\n", g_bbct.sensorType());
      g_bbctInited = true;
    }
  }
  return;
#else
  pinMode(BUTTON_ADC_PIN_1, INPUT);
  pinMode(BUTTON_ADC_PIN_2, INPUT);
  pinMode(POWER_BUTTON_PIN, INPUT_PULLUP);
  analogSetAttenuation(ADC_11db);
#endif
}

int Buttons::getButtonFromADC(int adcValue, const int thresholds[], int numButtons) {
  if (adcValue > ADC_NO_BUTTON) {
    return -1;
  }

  for (int i = 0; i < numButtons; i++) {
    if (abs(adcValue - thresholds[i]) < ADC_TOLERANCE) {
      return i;
    }
  }

  return -1;
}

uint8_t Buttons::getState() {
  uint8_t state = 0;

#ifdef USE_M5UNIFIED
  if (!g_bbctInited) {
    return state;
  }

  TOUCHINFO ti;
  g_bbct.getSamples(&ti);
  if (ti.count <= 0) {
    return state;
  }

  // Two-finger touch as a global BACK.
  if (ti.count >= 2) {
    state |= (1 << BACK);
    return state;
  }

  // FastEPD's Paper S3 example notes X/Y are reversed relative to the display.
  // Convert into our portrait coordinate system (0..DISPLAY_WIDTH-1, 0..DISPLAY_HEIGHT-1).
  const int16_t x = (int16_t)EInkDisplay::DISPLAY_WIDTH - 1 - (int16_t)ti.x[0];
  const int16_t y = (int16_t)EInkDisplay::DISPLAY_HEIGHT - 1 - (int16_t)ti.y[0];

  if (x < 0 || x >= (int16_t)EInkDisplay::DISPLAY_WIDTH || y < 0 || y >= (int16_t)EInkDisplay::DISPLAY_HEIGHT) {
    return state;
  }

  // Screen-zone mapping depends on orientation
  // Portrait (0): Left/Right thirds for page navigation
  // Landscape (1): Top/Bottom thirds for page navigation (rotated 90° CW)
  const int16_t backZoneW = 90;
  const int16_t backZoneH = 90;

  if (_orientation == 0) {
    // Portrait mode: original layout
    // - Top-left corner: BACK
    // - Left third: LEFT (next page)
    // - Right third: RIGHT (prev page)
    // - Middle: CONFIRM
    if (x < backZoneW && y < backZoneH) {
      state |= (1 << BACK);
      return state;
    }

    const int16_t w = (int16_t)EInkDisplay::DISPLAY_WIDTH;
    if (x < (w / 3)) {
      state |= (1 << LEFT);
    } else if (x >= (w - (w / 3))) {
      state |= (1 << RIGHT);
    } else {
      state |= (1 << CONFIRM);
    }
  } else {
    // Landscape mode: rotated layout
    // In landscape CW, the physical screen is rotated so:
    // - Top-left corner (in portrait coords): BACK
    // - Top third (portrait Y): LEFT (next page) - maps to left side in landscape view
    // - Bottom third (portrait Y): RIGHT (prev page) - maps to right side in landscape view
    // - Middle: CONFIRM
    if (x < backZoneW && y < backZoneH) {
      state |= (1 << BACK);
      return state;
    }

    const int16_t h = (int16_t)EInkDisplay::DISPLAY_HEIGHT;
    if (y < (h / 3)) {
      state |= (1 << LEFT);  // Top in portrait = Left in landscape
    } else if (y >= (h - (h / 3))) {
      state |= (1 << RIGHT);  // Bottom in portrait = Right in landscape
    } else {
      state |= (1 << CONFIRM);
    }
  }

  return state;
#else

  // Read GPIO1 buttons
  int adcValue1 = analogRead(BUTTON_ADC_PIN_1);
  int button1 = getButtonFromADC(adcValue1, ADC_THRESHOLDS_1, NUM_BUTTONS_1);
  if (button1 >= 0) {
    state |= (1 << button1);
  }

  // Read GPIO2 buttons
  int adcValue2 = analogRead(BUTTON_ADC_PIN_2);
  int button2 = getButtonFromADC(adcValue2, ADC_THRESHOLDS_2, NUM_BUTTONS_2);
  if (button2 >= 0) {
    state |= (1 << (button2 + 4));
  }

  // Read power button (digital, active LOW)
  if (digitalRead(POWER_BUTTON_PIN) == LOW) {
    state |= (1 << POWER);
  }

  return state;
#endif
}

void Buttons::update() {
  unsigned long currentTime = millis();
  
  // Update touch state tracking
  _prevTouchActive = _touchActive;
  
#ifdef USE_M5UNIFIED
  if (g_bbctInited) {
    TOUCHINFO ti;
    g_bbct.getSamples(&ti);
    _touchCount = ti.count;
    if (ti.count > 0) {
      _touchActive = true;
      // bb_captouch coordinates already match the display coordinate system.
      // Do not invert, otherwise touches are mirrored and hit-testing becomes unreliable.
      _touchX = (int16_t)ti.x[0];
      _touchY = (int16_t)ti.y[0];
    } else {
      _touchActive = false;
    }
  }
#endif
  
  // Use cached touch state instead of calling getState() which would read touch again
  uint8_t rawState = 0;
#ifdef USE_M5UNIFIED
  // Map cached touch coordinates to button zones
  if (_touchActive) {
    // Two-finger touch as a global BACK
    if (_touchCount >= 2) {
      rawState |= (1 << BACK);
    } else {
      if (!_zoneNavigationEnabled) {
        // Screen is doing its own touch handling (e.g. keyboard). Don't map to zones.
      } else {
      const int16_t x = _touchX;
      const int16_t y = _touchY;
      
      if (x >= 0 && x < (int16_t)EInkDisplay::DISPLAY_WIDTH && 
          y >= 0 && y < (int16_t)EInkDisplay::DISPLAY_HEIGHT) {
        const int16_t backZoneW = 90;
        const int16_t backZoneH = 90;
        
        if (_orientation == 0) {
          // Portrait mode
          if (x < backZoneW && y < backZoneH) {
            rawState |= (1 << BACK);
          } else {
            const int16_t w = (int16_t)EInkDisplay::DISPLAY_WIDTH;
            if (x < (w / 3)) {
              rawState |= (1 << RIGHT);
            } else if (x >= (w - (w / 3))) {
              rawState |= (1 << LEFT);
            } else {
              rawState |= (1 << CONFIRM);
            }
          }
        } else {
          // Landscape mode
          if (x < backZoneW && y < backZoneH) {
            rawState |= (1 << BACK);
          } else {
            const int16_t h = (int16_t)EInkDisplay::DISPLAY_HEIGHT;
            if (y < (h / 3)) {
              rawState |= (1 << RIGHT);
            } else if (y >= (h - (h / 3))) {
              rawState |= (1 << LEFT);
            } else {
              rawState |= (1 << CONFIRM);
            }
          }
        }
      }
      }
    }
  }
#else
  rawState = getState();
#endif

  // Save current state as previous before updating
  previousState = currentState;

  // Per-button debouncing (only for press, not release)
  for (uint8_t i = 0; i < NUM_BUTTONS; i++) {
    uint8_t buttonMask = (1 << i);
    uint8_t rawButtonState = (rawState & buttonMask) ? 1 : 0;
    uint8_t currentButtonState = (currentState & buttonMask) ? 1 : 0;
    uint8_t lastRawState = lastButtonState[i];

    // If raw state changed, reset debounce timer for this button
    if (rawButtonState != lastRawState) {
      lastDebounceTime[i] = currentTime;
      lastButtonState[i] = rawButtonState;
    }

    // Handle press with debounce, release immediately
    if (rawButtonState && !currentButtonState) {
      // Button is being pressed - wait for debounce
      if ((currentTime - lastDebounceTime[i]) > DEBOUNCE_DELAY) {
        currentState |= buttonMask;
      }
    } else if (!rawButtonState && currentButtonState) {
      // Button is being released - update immediately
      currentState &= ~buttonMask;
    }
  }
}

bool Buttons::isDown(uint8_t buttonIndex) {
  return currentState & (1 << buttonIndex);
}

bool Buttons::isPressed(uint8_t buttonIndex) {
  uint8_t mask = (1 << buttonIndex);
  return (currentState & mask) && !(previousState & mask);
}

bool Buttons::wasDown(uint8_t buttonIndex) {
  return previousState & (1 << buttonIndex);
}

bool Buttons::wasReleased(uint8_t buttonIndex) {
  uint8_t mask = (1 << buttonIndex);
  return !(currentState & mask) && (previousState & mask);
}

bool Buttons::wasAnyPressed() {
  return (currentState & ~previousState) != 0;
}

bool Buttons::wasAnyReleased() {
  return (~currentState & previousState) != 0;
}

const char* Buttons::getButtonName(uint8_t buttonIndex) {
  if (buttonIndex <= POWER) {
    return BUTTON_NAMES[buttonIndex];
  }
  return "Unknown";
}

bool Buttons::isPowerButtonDown() {
  return isDown(POWER);
}

unsigned long Buttons::getHoldDuration(uint8_t buttonIndex) {
  if (!isDown(buttonIndex)) {
    return 0;  // Button not held
  }
  return millis() - lastDebounceTime[buttonIndex];
}

bool Buttons::getTouchPosition(int16_t& x, int16_t& y) {
#ifdef USE_M5UNIFIED
  if (_touchActive) {
    x = _touchX;
    y = _touchY;
    return true;
  }
#endif
  (void)x;
  (void)y;
  return false;
}

bool Buttons::wasTouchReleased() {
  return _prevTouchActive && !_touchActive;
}
