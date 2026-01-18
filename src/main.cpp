#include <Arduino.h>
#include <FS.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#ifdef USE_M5UNIFIED
#include <M5Unified.h>
#endif

#include "core/BatteryMonitor.h"
#include "core/Buttons.h"
#include "core/EInkDisplay.h"
#include "core/SDCardManager.h"
#include "core/Settings.h"
#include "rendering/SimpleFont.h"
#include "resources/fonts/FontDefinitions.h"
#include "resources/fonts/other/MenuFontSmall.h"
#include "resources/fonts/other/MenuHeader.h"
#include "ui/UIManager.h"

Buttons buttons;

#ifdef USE_M5UNIFIED
EInkDisplay einkDisplay(-1, -1, -1, -1, -1, -1);
SDCardManager sdManager(0, 0, 0, 0, 0);
// Battery ADC pin and global instance (ignored on Paper S3; uses M5.Power)
BatteryMonitor g_battery(0);

#else
// USB detection pin
#define UART0_RXD 20  // Used for USB connection detection

// Power button timing
const unsigned long POWER_BUTTON_WAKEUP_MS = 250;  // Time required to confirm boot from sleep
// Power button pin (used in multiple places)
const int POWER_BUTTON_PIN = 3;

// Display SPI pins (custom pins, not hardware SPI defaults)
#define EPD_SCLK 8   // SPI Clock
#define EPD_DC 4     // Data/Command
#define EPD_RST 5    // Reset
#define EPD_BUSY 6   // Busy
#define EPD_MOSI 10  // SPI MOSI (Master Out Slave In)

#define SD_SPI_CS 12  // SD Card Chip Select
#define SD_SPI_MISO 7

#define EINK_SPI_CS 21  // EINK Chip Select

EInkDisplay einkDisplay(EPD_SCLK, EPD_MOSI, EINK_SPI_CS, EPD_DC, EPD_RST, EPD_BUSY);
SDCardManager sdManager(EPD_SCLK, SD_SPI_MISO, EPD_MOSI, SD_SPI_CS, EINK_SPI_CS);

// Battery ADC pin and global instance
#define BAT_GPIO0 0
BatteryMonitor g_battery(BAT_GPIO0);
#endif
UIManager* uiManager = nullptr;

static unsigned long getSleepTimeoutMs() {
  // 0=1 min, 1=5 min, 2=10 min, 3=15 min, 4=30 min
  int idx = 2;
  if (uiManager) {
    Settings& s = uiManager->getSettings();
    (void)s.getInt(String("settings.sleepTimeout"), idx);
  }
  switch (idx) {
    case 0:
      return 1UL * 60UL * 1000UL;
    case 1:
      return 5UL * 60UL * 1000UL;
    case 2:
    default:
      return 10UL * 60UL * 1000UL;
    case 3:
      return 15UL * 60UL * 1000UL;
    case 4:
      return 30UL * 60UL * 1000UL;
  }
}

// Button update task - runs continuously to keep button state fresh
void buttonUpdateTask(void* parameter) {
  Buttons* btns = static_cast<Buttons*>(parameter);
  while (true) {
    btns->update();
    vTaskDelay(pdMS_TO_TICKS(20));  // Update every 20ms
  }
}

// Write debug log to SD card
void writeDebugLog() {
#ifdef USE_M5UNIFIED
  return;
#else
  esp_sleep_wakeup_cause_t w = esp_sleep_get_wakeup_cause();
  String dbg = String("wakeup: ") + String((int)w) + "\n";
  dbg += String("power_raw: ") + String(digitalRead(POWER_BUTTON_PIN)) + "\n";

  if (sdManager.ready()) {
    if (!sdManager.writeFile("/log.txt", dbg)) {
      Serial.println("Failed to write log.txt to SD");
    }
  } else {
    Serial.println("SD not ready; skipping debug log write");
  }
#endif
}

// Check if USB is connected
bool isUsbConnected() {
#ifdef USE_M5UNIFIED
  return true;
#else
  // U0RXD/GPIO20 reads HIGH when USB is connected
  return digitalRead(UART0_RXD) == HIGH;
#endif
}

// Verify long press on wake-up
void verifyWakeupLongPress() {
#ifdef USE_M5UNIFIED
  return;
#else
  unsigned long timerStart = millis();
  long pressDuration = 0;
  bool bootDevice = false;

  pinMode(POWER_BUTTON_PIN, INPUT_PULLUP);

  // Monitor button state for the duration
  while (millis() - timerStart < POWER_BUTTON_WAKEUP_MS * 2) {
    delay(10);
    if (digitalRead(POWER_BUTTON_PIN) == LOW) {
      pressDuration += 10;

      if (pressDuration >= POWER_BUTTON_WAKEUP_MS) {
        // Long press detected; normal boot
        bootDevice = true;
        break;
      }
    } else {
      // Button released; reset timer
      pressDuration = 0;
    }
  }

  if (!bootDevice) {
    // Enable wakeup on power button (active LOW)
    pinMode(POWER_BUTTON_PIN, INPUT_PULLUP);
    esp_deep_sleep_enable_gpio_wakeup(1ULL << POWER_BUTTON_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);
    esp_deep_sleep_start();
  }
#endif
}

// Enter deep sleep mode
void enterDeepSleep() {
  Serial.println("Power button long press detected. Entering deep sleep.");

  // Let UI save any persistent state before we render the sleep screen
  if (uiManager)
    uiManager->prepareForSleep();

  // Show sleep screen
  if (uiManager)
    uiManager->showSleepScreen();

#ifdef USE_M5UNIFIED
  return;
#else

  // Enter deep sleep mode
  // this seems to start the display and leads to grayish screen somehow???
  // einkDisplay.deepSleep();
  // Serial.println("Entering deep sleep mode...");
  // delay(10);  // Allow serial buffer to empty

  // Enable wakeup on power button (active LOW)
  pinMode(POWER_BUTTON_PIN, INPUT_PULLUP);
  esp_deep_sleep_enable_gpio_wakeup(1ULL << POWER_BUTTON_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);
  esp_deep_sleep_start();
#endif
}

void setup() {
#ifdef USE_M5UNIFIED
  Serial.begin(115200);
  // Paper S3: Do NOT call M5.begin() - it initializes the display and takes the i80 bus,
  // which conflicts with FastEPD. Battery monitoring uses direct I2C to AXP2101 instead.
  // Keep power enabled via GPIO 2 (not 44 - that's for other boards)
  pinMode(2, OUTPUT);
  digitalWrite(2, HIGH);
#else
  // Only start/wait for serial monitor if USB is connected
  pinMode(UART0_RXD, INPUT);
  if (isUsbConnected()) {
    Serial.begin(115200);

    unsigned long start = millis();
    while (!Serial && (millis() - start) < 3000) {
      delay(10);
    }
  } else {
    verifyWakeupLongPress();
  }
#endif

  Serial.println("\n=================================");
  Serial.println("  MicroReader - ESP32-C3 E-Ink");
  Serial.println("=================================");
  Serial.println();

  // Initialize buttons
  Serial.println("Init: Buttons...");
  buttons.begin();
  Serial.println("Buttons initialized");

  // Start button update task
  Serial.println("Init: Button task...");
#ifdef USE_M5UNIFIED
  // Paper S3 touch uses I2C (Wire) under the hood. Polling touch from a background task
  // can lead to I2C driver invalid state / watchdog issues while the main thread is doing
  // heavy work (e.g. EPUB extraction/parsing). Keep input polling single-threaded by
  // updating buttons from loop() only.
#else
  xTaskCreate(buttonUpdateTask, "btnUpdate", 2048, &buttons, 1, nullptr);
#endif
  Serial.println("Button update task started");

  // Initialize SD card manager
  Serial.println("Init: SD Card...");
  sdManager.begin();

  // Ensure required directories exist
  if (sdManager.ready()) {
    sdManager.ensureDirectoryExists("/microreader");
    sdManager.ensureDirectoryExists("/books");
  }
  Serial.println("SD Card initialized");

  // Write debug log
  // writeDebugLog();

  // Initialize display driver FIRST (allocate frame buffers before EPUB test to avoid fragmentation)
  Serial.printf("Free memory before display init: %d bytes\n", ESP.getFreeHeap());
  Serial.println("Init: Display...");
  einkDisplay.begin();
  Serial.println("Display initialized");

  // Initialize display controller (handles application logic)
  uiManager = new UIManager(einkDisplay, sdManager, buttons);
  uiManager->begin();

  Serial.println("Initialization complete!\n");
}

void loop() {
  // Paper S3: update touch/buttons from the main loop to keep I2C access single-threaded.
#ifdef USE_M5UNIFIED
  static unsigned long lastBtnPollMs = 0;
  const unsigned long nowMs = millis();
  if (nowMs - lastBtnPollMs >= 20) {
    buttons.update();
    lastBtnPollMs = nowMs;
  }
#endif
  // Print memory stats every second
  static unsigned long lastMemPrint = 0;
  if (Serial && millis() - lastMemPrint >= 4000) {
    Serial.printf("[%lu] Memory - Free: %d bytes, Total: %d bytes, Min Free: %d bytes\n", millis(), ESP.getFreeHeap(),
                  ESP.getHeapSize(), ESP.getMinFreeHeap());
    lastMemPrint = millis();
  }

  // Button state is updated by background task
  if (uiManager)
    uiManager->handleButtons();

  // Auto-sleep after inactivity (skip when USB is connected)
  static unsigned long lastActivityTime = millis();
  if (buttons.wasAnyPressed() || buttons.wasAnyReleased()) {
    lastActivityTime = millis();
  }
  if (!isUsbConnected()) {
    const unsigned long sleepTimeoutMs = getSleepTimeoutMs();
    if (millis() - lastActivityTime >= sleepTimeoutMs) {
      Serial.printf("[%lu] Auto-sleep triggered after %lu ms of inactivity\n", millis(), sleepTimeoutMs);
      enterDeepSleep();
      return;
    }
  }

  // Check for power button press to enter sleep
  if (buttons.isPowerButtonDown()) {
    enterDeepSleep();
  }

  // Small delay to avoid busy loop
  delay(10);
}
