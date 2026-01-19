#include "WifiPasswordEntryScreen.h"

#include <resources/fonts/FontManager.h>
#include <resources/fonts/other/MenuFontSmall.h>
#include <resources/fonts/other/MenuHeader.h>

#include "../../core/Buttons.h"
#include "../../core/Settings.h"
#include "../UIManager.h"

// Special "keys"; normal keys are just single printable chars.
static const char kKeyOk = '\x01';
static const char kKeyDel = '\x02';
static const char kKeySpace = '\x03';
static const char kKeyShift = '\x04';
static const char kKeySym = '\x05';

static const char kKeyboardAlpha[5][10] = {
    // Row0: digits
    {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0'},
    // Row1
    {'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p'},
    // Row2
    {'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', kKeyDel},
    // Row3
    {kKeyShift, 'z', 'x', 'c', 'v', 'b', 'n', 'm', '-', '_'},
    // Row4
    {kKeyOk, kKeySpace, '.', '@', '/', '\\', ':', ';', kKeySym, '!'},
};

// Symbols layout: keep digits top row, make rest punctuation-heavy.
static const char kKeyboardSym[5][10] = {
    {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0'},
    {'!', '@', '#', '$', '%', '^', '&', '*', '(', ')'},
    {'-', '_', '=', '+', '[', ']', '{', '}', '\\', kKeyDel},
    {kKeyShift, '<', '>', '?', '/', '|', '~', '`', '"', '\''},
    {kKeyOk, kKeySpace, '.', ',', ':', ';', kKeySym, '!', '&', '='},
};

static char getKeyAt(bool symbols, bool caps, int row, int col) {
  if (row < 0 || row >= 5 || col < 0 || col >= 10)
    return 0;
  char k = symbols ? kKeyboardSym[row][col] : kKeyboardAlpha[row][col];
  if (!symbols && caps && k >= 'a' && k <= 'z') {
    k = (char)('A' + (k - 'a'));
  }
  return k;
}

static const char* getKeyLabel(char key) {
  switch (key) {
    case kKeyOk:
      return "ENTER";
    case kKeyDel:
      return "BKSP";
    case kKeySpace:
      return "SPACE";
    case kKeyShift:
      return "SHIFT";
    case kKeySym:
      return "SYM";
    default:
      return nullptr;
  }
}

static void debugPrintKey(char key) {
  if (key == 0) {
    Serial.print("key=0");
    return;
  }
  const char* label = getKeyLabel(key);
  if (label) {
    Serial.printf("key=%d(%s)", (int)key, label);
    return;
  }
  if (key >= 32 && key <= 126) {
    Serial.printf("key='%c'(%d)", key, (int)key);
    return;
  }
  Serial.printf("key=%d", (int)key);
}

static int getRowKeyCount(bool symbols, int row) {
  (void)symbols;
  switch (row) {
    case 0:
      return 10;
    case 1:
      return 10;
    case 2:
      return 10;
    case 3:
      return 10;
    case 4:
      return 6;
    default:
      return 0;
  }
}

WifiPasswordEntryScreen::WifiPasswordEntryScreen(EInkDisplay& display, TextRenderer& renderer, UIManager& uiManager)
    : display(display), textRenderer(renderer), uiManager(uiManager) {}

void WifiPasswordEntryScreen::begin() {
  loadSettings();
}

void WifiPasswordEntryScreen::activate() {
  loadSettings();
  editOriginal = wifiPass;
  editBuffer = wifiPass;
  keyRow = 1;
  keyCol = 0;
  caps = false;
  symbols = false;
  lastTouchX = -1;
  lastTouchY = -1;
  touchPressed = false;
  pressedRow = -1;
  pressedCol = -1;
}

void WifiPasswordEntryScreen::handleButtons(Buttons& buttons) {
  static uint32_t lastTouchLogMs = 0;
  static int lastLogRow = -2;
  static int lastLogCol = -2;
  static bool lastLogTouching = false;

  // Handle back gesture (two-finger tap)
  if (buttons.isPressed(Buttons::BACK)) {
    Serial.printf("[%lu] WifiPasswordEntry: BACK gesture\n", millis());
    editBuffer = editOriginal;
    uiManager.showScreen(UIManager::ScreenId::WifiSettings);
    return;
  }
  
  // Handle touch input for keyboard
  int16_t touchX, touchY;
  const bool touching = buttons.getTouchPosition(touchX, touchY);
  if (touching) {
    lastTouchX = touchX;
    lastTouchY = touchY;

    int hitRow, hitCol;
    const bool hit = hitTestKey(touchX, touchY, hitRow, hitCol);

    const uint32_t nowMs = millis();
    if (nowMs - lastTouchLogMs >= 120 || lastLogTouching != touching || hitRow != lastLogRow || hitCol != lastLogCol) {
      lastTouchLogMs = nowMs;
      lastLogTouching = touching;
      lastLogRow = hit ? hitRow : -1;
      lastLogCol = hit ? hitCol : -1;
      Serial.printf("[%lu] WifiPasswordEntry: touch=(%d,%d) hit=%d row=%d col=%d ", nowMs, (int)touchX, (int)touchY,
                    hit ? 1 : 0, hit ? hitRow : -1, hit ? hitCol : -1);
      if (hit) {
        int x, y, w, h;
        char key;
        const char* label;
        if (getKeyRect(hitRow, hitCol, x, y, w, h, key, label)) {
          (void)label;
          Serial.printf("rect=[%d,%d %dx%d] ", x, y, w, h);
          debugPrintKey(key);
        } else {
          Serial.print("rect=?");
        }
      }
      Serial.printf(" pressed=(%d,%d) bufLen=%d\n", pressedRow, pressedCol, (int)editBuffer.length());
    }

    if (!touchPressed) {
      touchPressed = true;
      pressedRow = hit ? hitRow : -1;
      pressedCol = hit ? hitCol : -1;
      if (hit) {
        keyRow = hitRow;
        keyCol = hitCol;
        Serial.printf("[%lu] WifiPasswordEntry: press row=%d col=%d\n", millis(), pressedRow, pressedCol);
        show();
      }
    } else {
      if (hit && (hitRow != pressedRow || hitCol != pressedCol)) {
        pressedRow = hitRow;
        pressedCol = hitCol;
        keyRow = hitRow;
        keyCol = hitCol;
        Serial.printf("[%lu] WifiPasswordEntry: move row=%d col=%d\n", millis(), pressedRow, pressedCol);
        show();
      }
      if (!hit && (pressedRow != -1 || pressedCol != -1)) {
        pressedRow = -1;
        pressedCol = -1;
        Serial.printf("[%lu] WifiPasswordEntry: move off keys\n", millis());
        show();
      }
    }
  }

  if (buttons.wasTouchReleased()) {
    int relRow, relCol;
    const bool relHit = (lastTouchX >= 0 && lastTouchY >= 0) && hitTestKey(lastTouchX, lastTouchY, relRow, relCol);
    Serial.printf("[%lu] WifiPasswordEntry: release last=(%d,%d) relHit=%d rel=(%d,%d) pressed=(%d,%d)\n", millis(),
                  (int)lastTouchX, (int)lastTouchY, relHit ? 1 : 0, relHit ? relRow : -1, relHit ? relCol : -1,
                  pressedRow, pressedCol);
    if (touchPressed && relHit && relRow == pressedRow && relCol == pressedCol) {
      keyRow = relRow;
      keyCol = relCol;
      chooseKeyAt(relRow, relCol);
    } else {
      show();
    }

    touchPressed = false;
    pressedRow = -1;
    pressedCol = -1;
    lastTouchX = -1;
    lastTouchY = -1;
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

void WifiPasswordEntryScreen::chooseKey() {
  chooseKeyAt(keyRow, keyCol);
}

void WifiPasswordEntryScreen::chooseKeyAt(int row, int col) {
  int x, y, w, h;
  char key;
  const char* label;
  if (!getKeyRect(row, col, x, y, w, h, key, label)) {
    Serial.printf("[%lu] WifiPasswordEntry: chooseKeyAt row=%d col=%d -> no rect\n", millis(), row, col);
    return;
  }
  (void)x;
  (void)y;
  (void)w;
  (void)h;
  (void)label;
  Serial.printf("[%lu] WifiPasswordEntry: chooseKeyAt row=%d col=%d -> ", millis(), row, col);
  debugPrintKey(key);
  Serial.print("\n");
  chooseKeyCode(key);
}

void WifiPasswordEntryScreen::chooseKeyCode(char key) {
  if (key == 0) return;

  Serial.printf("[%lu] WifiPasswordEntry: chooseKeyCode ", millis());
  debugPrintKey(key);
  Serial.printf(" bufLenBefore=%d\n", (int)editBuffer.length());

  if (key == kKeyOk) {
    wifiPass = editBuffer;
    saveSettings();
    uiManager.showScreen(UIManager::ScreenId::WifiSettings);
    return;
  }

  if (key == kKeyDel) {
    if (editBuffer.length() > 0) {
      editBuffer.remove(editBuffer.length() - 1);
    }
    Serial.printf("[%lu] WifiPasswordEntry: BKSP bufLenAfter=%d\n", millis(), (int)editBuffer.length());
    show();
    return;
  }

  if (key == kKeySpace) {
    if (editBuffer.length() < 64) {
      editBuffer += ' ';
    }
    Serial.printf("[%lu] WifiPasswordEntry: SPACE bufLenAfter=%d\n", millis(), (int)editBuffer.length());
    show();
    return;
  }

  if (key == kKeyShift) {
    caps = !caps;
    Serial.printf("[%lu] WifiPasswordEntry: SHIFT caps=%d\n", millis(), caps ? 1 : 0);
    show();
    return;
  }

  if (key == kKeySym) {
    symbols = !symbols;
    Serial.printf("[%lu] WifiPasswordEntry: SYM symbols=%d\n", millis(), symbols ? 1 : 0);
    show();
    return;
  }

  if (key >= 32 && key <= 126 && editBuffer.length() < 64) {
    editBuffer += key;
  }
  Serial.printf("[%lu] WifiPasswordEntry: CHAR bufLenAfter=%d\n", millis(), (int)editBuffer.length());
  show();
}

bool WifiPasswordEntryScreen::getKeyRect(int row, int col, int& x, int& y, int& w, int& h, char& keyOut, const char*& labelOut) {
  keyOut = 0;
  labelOut = nullptr;
  if (row < 0 || row >= kKeyboardRows) return false;
  const int count = getRowKeyCount(symbols, row);
  if (col < 0 || col >= count) return false;

  const int marginX = 12;
  const int keyboardW = (int)EInkDisplay::DISPLAY_WIDTH - 2 * marginX;

  static const uint8_t row4Keys[6] = {kKeySym, kKeySpace, '.', '@', '/', kKeyOk};
  static const uint8_t row4Units[6] = {2, 4, 1, 1, 1, 2};
  static const uint8_t row2Units[10] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 2};
  static const uint8_t row3Units[10] = {2, 1, 1, 1, 1, 1, 1, 1, 1, 1};

  const uint8_t* units = nullptr;
  int totalUnits = 0;

  if (row == 4) {
    units = row4Units;
    for (int i = 0; i < 6; i++) totalUnits += row4Units[i];
  } else if (row == 2) {
    units = row2Units;
    for (int i = 0; i < 10; i++) totalUnits += row2Units[i];
  } else if (row == 3) {
    units = row3Units;
    for (int i = 0; i < 10; i++) totalUnits += row3Units[i];
  } else {
    totalUnits = count;
  }

  const int gaps = count - 1;
  const int usableW = keyboardW - gaps * kKeySpacing;
  const int unitW = (totalUnits > 0) ? (usableW / totalUnits) : 0;
  const int extra = (totalUnits > 0) ? (usableW - unitW * totalUnits) : 0;

  int cursorX = marginX;
  int unitAccum = 0;
  for (int i = 0; i < col; i++) {
    const int u = units ? units[i] : 1;
    unitAccum += u;
    cursorX += u * unitW;
    if (unitAccum <= extra) cursorX += 1;
    cursorX += kKeySpacing;
  }

  const int u = units ? units[col] : 1;
  int ww = u * unitW;
  unitAccum += u;
  if (unitAccum <= extra) ww += 1;

  x = cursorX;
  y = kKeyboardStartY + row * (kKeyHeight + kKeySpacing);
  w = ww;
  h = kKeyHeight;

  if (row == 4) {
    keyOut = (char)row4Keys[col];
  } else {
    keyOut = getKeyAt(symbols, caps, row, col);
  }
  labelOut = getKeyLabel(keyOut);
  return keyOut != 0;
}

bool WifiPasswordEntryScreen::hitTestKey(int16_t touchX, int16_t touchY, int& outRow, int& outCol) {
  if (touchY < kKeyboardStartY) return false;
  const int row = (touchY - kKeyboardStartY) / (kKeyHeight + kKeySpacing);
  if (row < 0 || row >= kKeyboardRows) return false;

  const int count = getRowKeyCount(symbols, row);
  for (int col = 0; col < count; col++) {
    int x, y, w, h;
    char key;
    const char* label;
    if (!getKeyRect(row, col, x, y, w, h, key, label)) continue;
    (void)key;
    (void)label;
    if (touchX >= x && touchX < x + w && touchY >= y && touchY < y + h) {
      outRow = row;
      outCol = col;
      return true;
    }
  }
  return false;
}

void WifiPasswordEntryScreen::renderKey(char key, const char* labelIn, int x, int y, int w, int h, bool selected) {
  if (key == 0) return;
  
  // Draw key border (thicker for selected)
  uint8_t* fb = display.getFrameBuffer();
  
  // Draw rectangle border
  for (int px = x; px < x + w; px++) {
    // Top and bottom edges
    for (int thickness = 0; thickness < (selected ? 3 : 1); thickness++) {
      int topY = y + thickness;
      int botY = y + h - 1 - thickness;
      if (topY < EInkDisplay::DISPLAY_HEIGHT && botY >= 0) {
        int topIdx = topY * EInkDisplay::DISPLAY_WIDTH_BYTES + (px / 8);
        int botIdx = botY * EInkDisplay::DISPLAY_WIDTH_BYTES + (px / 8);
        uint8_t mask = 0x80 >> (px & 7);
        fb[topIdx] &= ~mask;
        fb[botIdx] &= ~mask;
      }
    }
  }
  for (int py = y; py < y + h; py++) {
    // Left and right edges
    for (int thickness = 0; thickness < (selected ? 3 : 1); thickness++) {
      int leftX = x + thickness;
      int rightX = x + w - 1 - thickness;
      if (py >= 0 && py < EInkDisplay::DISPLAY_HEIGHT) {
        int leftIdx = py * EInkDisplay::DISPLAY_WIDTH_BYTES + (leftX / 8);
        int rightIdx = py * EInkDisplay::DISPLAY_WIDTH_BYTES + (rightX / 8);
        uint8_t leftMask = 0x80 >> (leftX & 7);
        uint8_t rightMask = 0x80 >> (rightX & 7);
        fb[leftIdx] &= ~leftMask;
        fb[rightIdx] &= ~rightMask;
      }
    }
  }
  
  const char* special = labelIn ? labelIn : getKeyLabel(key);
  String label;
  if (special) {
    label = String(special);
  } else {
    label = String((char)key);
  }
  
  // Center label in key
  int16_t x1, y1;
  uint16_t tw, th;
  textRenderer.getTextBounds(label.c_str(), 0, 0, &x1, &y1, &tw, &th);
  int labelX = x + (w - tw) / 2 - x1;
  int labelY = y + (h + th) / 2;
  
  textRenderer.setCursor(labelX, labelY);
  textRenderer.print(label);
}

void WifiPasswordEntryScreen::render() {
  display.clearScreen(0xFF);
  textRenderer.setTextColor(TextRenderer::COLOR_BLACK);
  textRenderer.setFont(getTitleFont());

  textRenderer.setFrameBuffer(display.getFrameBuffer());
  textRenderer.setBitmapType(TextRenderer::BITMAP_BW);

  uiManager.renderStatusHeader(textRenderer);

  textRenderer.setFont(getTitleFont());

  // Title
  {
    const char* title = "WiFi Password";
    int16_t x1, y1;
    uint16_t w, h;
    textRenderer.getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
    int16_t centerX = (EInkDisplay::DISPLAY_WIDTH - (int)w) / 2;
    textRenderer.setCursor(centerX, 90);
    textRenderer.print(title);
  }

  textRenderer.setFont(getMainFont());

  // Password display with cursor
  {
    String shown;
    for (size_t i = 0; i < editBuffer.length() && i < 24; ++i)
      shown += "*";
    if (editBuffer.length() > 24)
      shown += "...";
    shown += "_";  // Cursor

    int16_t x1, y1;
    uint16_t w, h;
    textRenderer.getTextBounds(shown.c_str(), 0, 0, &x1, &y1, &w, &h);
    int16_t centerX = (EInkDisplay::DISPLAY_WIDTH - (int)w) / 2;
    textRenderer.setCursor(centerX, 180);
    textRenderer.print(shown);
    
    // Show character count
    String countStr = String("(") + String(editBuffer.length()) + String(" chars)");
    textRenderer.getTextBounds(countStr.c_str(), 0, 0, &x1, &y1, &w, &h);
    centerX = (EInkDisplay::DISPLAY_WIDTH - (int)w) / 2;
    textRenderer.setCursor(centerX, 220);
    textRenderer.print(countStr);
  }

  // Mode indicators
  {
    String modeStr;
    if (caps) modeStr += "[CAPS] ";
    if (symbols) modeStr += "[SYM]";
    if (modeStr.length() > 0) {
      int16_t x1, y1;
      uint16_t w, h;
      textRenderer.getTextBounds(modeStr.c_str(), 0, 0, &x1, &y1, &w, &h);
      int16_t centerX = (EInkDisplay::DISPLAY_WIDTH - (int)w) / 2;
      textRenderer.setCursor(centerX, 260);
      textRenderer.print(modeStr);
    }
  }

  // Keyboard grid with touch-sized keys
  {
    for (int r = 0; r < kKeyboardRows; ++r) {
      const int count = getRowKeyCount(symbols, r);
      for (int c = 0; c < count; ++c) {
        int x, y, w, h;
        char key;
        const char* label;
        if (!getKeyRect(r, c, x, y, w, h, key, label)) continue;
        const bool selected = (touchPressed && r == pressedRow && c == pressedCol) || (!touchPressed && r == keyRow && c == keyCol);
        renderKey(key, label, x, y, w, h, selected);
      }
    }
  }

  // Instructions at bottom
  {
    textRenderer.setFont(&MenuFontSmall);
    const char* hint = "Tap keys to type. Two-finger tap to cancel.";
    int16_t x1, y1;
    uint16_t w, h;
    textRenderer.getTextBounds(hint, 0, 0, &x1, &y1, &w, &h);
    int16_t centerX = (EInkDisplay::DISPLAY_WIDTH - (int)w) / 2;
    textRenderer.setCursor(centerX, 920);
    textRenderer.print(hint);
  }
}
