#pragma once

#include <algorithm>
#include <cstring>
#include <fstream>
#include <string>

struct MockFile {
  std::string content;
  size_t currentPos = 0;
  bool isOpen = false;
  MockFile() {}
  ~MockFile() {
    close();
  }
  operator bool() const {
    return isOpen;
  }
  size_t size() {
    return content.size();
  }
  bool seek(size_t pos) {
    if (!isOpen)
      return false;
    currentPos = pos;
    return true;
  }
  size_t read(void* buf, size_t len) {
    if (!isOpen)
      return 0;
    size_t toRead = std::min(len, content.size() - currentPos);
    memcpy(buf, content.data() + currentPos, toRead);
    currentPos += toRead;
    return toRead;
  }
  void close() {
    isOpen = false;
    content.clear();
    currentPos = 0;
  }
};

struct MockSD {
  MockFile open(const char* path) {
    MockFile f;
    std::ifstream in(path, std::ios::binary);
    if (in.is_open()) {
      f.isOpen = true;
      std::string& content = f.content;
      in.seekg(0, std::ios::end);
      content.resize(in.tellg());
      in.seekg(0, std::ios::beg);
      in.read(content.data(), content.size());
      in.close();
    }
    return f;
  }
};

extern MockSD SD;
typedef MockFile File;