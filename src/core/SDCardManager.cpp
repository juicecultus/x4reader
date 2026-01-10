#include "SDCardManager.h"

#include <SD.h>
#include <SPI.h>

SDCardManager::SDCardManager(uint8_t epd_sclk, uint8_t sd_miso, uint8_t epd_mosi, uint8_t sd_cs, uint8_t eink_cs)
    : epd_sclk(epd_sclk), sd_miso(sd_miso), epd_mosi(epd_mosi), sd_cs(sd_cs), eink_cs(eink_cs), initialized(false) {}

bool SDCardManager::begin() {
  pinMode(eink_cs, OUTPUT);
  digitalWrite(eink_cs, HIGH);

  pinMode(sd_cs, OUTPUT);
  digitalWrite(sd_cs, HIGH);

  SPI.begin(epd_sclk, sd_miso, epd_mosi, sd_cs);
  if (!SD.begin(sd_cs, SPI, 40000000)) {
    Serial.print("\n SD card not detected\n");
    initialized = false;
  } else {
    Serial.print("\n SD card detected\n");
    initialized = true;
  }

  return initialized;
}

bool SDCardManager::ready() const {
  return initialized;
}

void SDCardManager::ensureSpiBusIdle() {
  // SD and the e-ink controller share the same SPI bus. If the e-ink CS is
  // left asserted, SD transactions can fail in the SD stack (sometimes
  // fatally). Force both devices deselected before any SD access.
  pinMode(eink_cs, OUTPUT);
  digitalWrite(eink_cs, HIGH);
  pinMode(sd_cs, OUTPUT);
  digitalWrite(sd_cs, HIGH);
}

bool SDCardManager::isDirectory(const char* path) {
  if (!initialized) {
    return false;
  }

  File f = SD.open(path);
  if (!f) {
    return false;
  }

  bool isDir = f.isDirectory();
  f.close();
  return isDir;
}

std::vector<String> SDCardManager::listFiles(const char* path, int maxFiles) {
  std::vector<String> ret;
  if (!initialized) {
    Serial.println("SDCardManager: not initialized, returning empty list");
    return ret;
  }

  ensureSpiBusIdle();

  File root = SD.open(path);
  if (!root) {
    Serial.printf("SDCardManager: Failed to open directory: %s\n", path);
    return ret;
  }
  if (!root.isDirectory()) {
    Serial.printf("SDCardManager: Path is not a directory: %s\n", path);
    root.close();
    return ret;
  }

  Serial.printf("SDCardManager: Scanning directory: %s\n", path);
  int count = 0;
  for (File f = root.openNextFile(); f && count < maxFiles; f = root.openNextFile()) {
    if (f.isDirectory()) {
      Serial.printf("  [DIR]  %s\n", f.name());
      f.close();
      continue;
    }
    String fname = String(f.name());
    Serial.printf("  [FILE] %s\n", fname.c_str());
    ret.push_back(fname);
    f.close();
    count++;
  }
  Serial.printf("SDCardManager: Found %d total files/dirs\n", count);
  root.close();
  return ret;
}

String SDCardManager::readFile(const char* path) {
  if (!initialized) {
    Serial.println("SDCardManager: not initialized; cannot read file");
    return String("");
  }

  ensureSpiBusIdle();

  File f = SD.open(path);
  if (!f) {
    Serial.printf("Failed to open file: %s\n", path);
    return String("");
  }

  String content = "";
  size_t maxSize = 50000;  // Limit to 50KB
  size_t readSize = 0;
  while (f.available() && readSize < maxSize) {
    char c = (char)f.read();
    content += c;
    readSize++;
  }
  f.close();
  return content;
}

bool SDCardManager::readFileToStream(const char* path, Print& out, size_t chunkSize) {
  if (!initialized) {
    Serial.println("SDCardManager: not initialized; cannot read file");
    return false;
  }

  ensureSpiBusIdle();

  File f = SD.open(path);
  if (!f) {
    Serial.printf("Failed to open file: %s\n", path);
    return false;
  }

  const size_t localBufSize = 256;
  uint8_t buf[localBufSize];
  size_t toRead = (chunkSize == 0) ? localBufSize : (chunkSize < localBufSize ? chunkSize : localBufSize);

  while (f.available()) {
    int r = f.read(buf, toRead);
    if (r > 0) {
      out.write(buf, (size_t)r);
    } else {
      break;
    }
  }

  f.close();
  return true;
}

size_t SDCardManager::readFileToBuffer(const char* path, char* buffer, size_t bufferSize, size_t maxBytes) {
  if (!buffer || bufferSize == 0)
    return 0;
  if (!initialized) {
    Serial.println("SDCardManager: not initialized; cannot read file");
    buffer[0] = '\0';
    return 0;
  }

  ensureSpiBusIdle();

  File f = SD.open(path);
  if (!f) {
    Serial.printf("Failed to open file: %s\n", path);
    buffer[0] = '\0';
    return 0;
  }

  size_t maxToRead = (maxBytes == 0) ? (bufferSize - 1) : min(maxBytes, bufferSize - 1);
  size_t total = 0;
  const size_t chunk = 64;

  while (f.available() && total < maxToRead) {
    size_t want = maxToRead - total;
    size_t readLen = (want < chunk) ? want : chunk;
    int r = f.read((uint8_t*)(buffer + total), readLen);
    if (r > 0) {
      total += (size_t)r;
    } else {
      break;
    }
  }

  buffer[total] = '\0';
  f.close();
  return total;
}

bool SDCardManager::writeFile(const char* path, const String& content) {
  if (!initialized) {
    Serial.println("SDCardManager: not initialized; cannot write file");
    return false;
  }

  ensureSpiBusIdle();

  // Remove existing file so we perform an overwrite rather than append
  if (SD.exists(path)) {
    SD.remove(path);
  }

  File f = SD.open(path, FILE_WRITE);
  if (!f) {
    Serial.printf("Failed to open file for write: %s\n", path);
    return false;
  }

  size_t written = f.print(content);
  f.close();
  return (written == content.length());
}

bool SDCardManager::ensureDirectoryExists(const char* path) {
  if (!initialized) {
    Serial.println("SDCardManager: not initialized; cannot create directory");
    return false;
  }

  ensureSpiBusIdle();

  // Check if directory already exists
  if (SD.exists(path)) {
    File dir = SD.open(path);
    if (dir && dir.isDirectory()) {
      dir.close();
      Serial.printf("Directory already exists: %s\n", path);
      return true;
    }
    dir.close();
  }

  // Create the directory
  if (SD.mkdir(path)) {
    Serial.printf("Created directory: %s\n", path);
    return true;
  } else {
    Serial.printf("Failed to create directory: %s\n", path);
    return false;
  }
}

bool SDCardManager::removeRecursive(const char* path) {
  if (!initialized) {
    Serial.println("SDCardManager: not initialized; cannot remove");
    return false;
  }
  if (!path || path[0] == '\0') {
    return false;
  }
  if (!SD.exists(path)) {
    return true;
  }

  File f = SD.open(path);
  if (!f) {
    Serial.printf("SDCardManager: removeRecursive failed to open: %s\n", path);
    return false;
  }

  if (!f.isDirectory()) {
    f.close();
    return SD.remove(path);
  }

  for (File child = f.openNextFile(); child; child = f.openNextFile()) {
    String childName = String(child.name());
    child.close();

    String childPath;
    if (childName.startsWith("/")) {
      childPath = childName;
    } else if (String(path) == String("/")) {
      childPath = String("/") + childName;
    } else {
      childPath = String(path) + String("/") + childName;
    }

    if (!removeRecursive(childPath.c_str())) {
      f.close();
      return false;
    }
  }

  f.close();
  return SD.rmdir(path);
}

bool SDCardManager::clearEpubExtractCache() {
  if (!initialized) {
    Serial.println("SDCardManager: not initialized; cannot clear cache");
    return false;
  }

  const char* rootPath = "/microreader";
  if (!SD.exists(rootPath)) {
    return true;
  }

  File root = SD.open(rootPath);
  if (!root || !root.isDirectory()) {
    if (root)
      root.close();
    return false;
  }

  bool ok = true;
  for (File entry = root.openNextFile(); entry; entry = root.openNextFile()) {
    if (!entry.isDirectory()) {
      entry.close();
      continue;
    }

    String name = String(entry.name());
    entry.close();

    String lname = name;
    lname.toLowerCase();

    int lastSlash = lname.lastIndexOf('/');
    String base = (lastSlash >= 0) ? lname.substring(lastSlash + 1) : lname;
    if (!base.startsWith("epub_")) {
      continue;
    }

    String dirPath = name.startsWith("/") ? name : (String(rootPath) + String("/") + name);
    Serial.printf("SDCardManager: Removing EPUB cache dir %s\n", dirPath.c_str());
    if (!removeRecursive(dirPath.c_str())) {
      ok = false;
    }
  }

  root.close();
  return ok;
}
