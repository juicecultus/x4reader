#include "StringWordProvider.h"

// NOTE: To keep this class testable without pulling in platform-specific
// graphics/Arduino headers, do not depend on TextRenderer methods here.
// Width measurement is simplified to the character count of the word so
// unit tests can instantiate and exercise this class without stubbing
// the entire graphics stack.

StringWordProvider::StringWordProvider(const String& text) : text_(text), index_(0), prevIndex_(0) {}

StringWordProvider::~StringWordProvider() {}

bool StringWordProvider::hasNextWord() {
  return index_ < text_.length();
}

String StringWordProvider::getNextWord() {
  return scanWord(+1);
}

String StringWordProvider::getPrevWord() {
  return scanWord(-1);
}

String StringWordProvider::scanWord(int direction) {
  // Save prevIndex_ when scanning
  prevIndex_ = index_;

  int currentPos = (direction == 1) ? index_ : index_ - 1;
  if ((direction == 1 && currentPos >= text_.length()) || (direction == -1 && currentPos < 0)) {
    return String("");
  }

  char c = text_[currentPos];

  if (c == ' ') {
    String token;
    int start = currentPos;
    if (direction == 1) {
      int end = currentPos;
      while (end < text_.length() && text_[end] == ' ')
        end++;
      token = text_.substring(start, end);
      index_ = end;
    } else {
      while (start > 0 && text_[start - 1] == ' ')
        start--;
      token = text_.substring(start, index_);
      index_ = start;
    }
    return token;
  } else if (c == '\r') {
    if (direction == 1) {
      index_++;
    } else {
      index_ = currentPos;
    }
    // Ignore carriage return
    return scanWord(direction);
  } else if (c == '\n' || c == '\t') {
    if (direction == 1) {
      index_++;
    } else {
      index_ = currentPos;
    }
    return String(c);
  } else {
    String token;
    int start = currentPos;
    if (direction == 1) {
      int end = currentPos;
      while (end < text_.length() && text_[end] != ' ' && text_[end] != '\n' && text_[end] != '\t')
        end++;
      token = text_.substring(start, end);
      index_ = end;
    } else {
      while (start > 0 && text_[start - 1] != ' ' && text_[start - 1] != '\n' && text_[start - 1] != '\t')
        start--;
      token = text_.substring(start, index_);
      index_ = start;
    }
    return token;
  }
}

float StringWordProvider::getPercentage() {
  if (text_.length() == 0)
    return 1.0f;
  return static_cast<float>(index_) / static_cast<float>(text_.length());
}

float StringWordProvider::getPercentage(int index) {
  if (text_.length() == 0)
    return 1.0f;
  return static_cast<float>(index) / static_cast<float>(text_.length());
}

int StringWordProvider::getCurrentIndex() {
  return index_;
}

char StringWordProvider::peekChar(int offset) {
  int pos = index_ + offset;
  if (pos < 0 || pos >= text_.length()) {
    return '\0';
  }
  return text_[pos];
}

bool StringWordProvider::isInsideWord() {
  if (index_ <= 0 || index_ >= text_.length()) {
    return false;
  }

  // Helper lambda to check if a character is a word character (not whitespace/control)
  auto isWordChar = [](char c) { return c != '\0' && c != ' ' && c != '\n' && c != '\t' && c != '\r'; };

  // Check character before current position
  char prevChar = text_[index_ - 1];
  // Check character at current position
  char currentChar = text_[index_];

  return isWordChar(prevChar) && isWordChar(currentChar);
}

void StringWordProvider::ungetWord() {
  index_ = prevIndex_;
}

void StringWordProvider::setPosition(int index) {
  if (index < 0)
    index = 0;
  if (index > text_.length())
    index = text_.length();

  index_ = index;
  prevIndex_ = index;
}

void StringWordProvider::reset() {
  index_ = 0;
  prevIndex_ = 0;
}