#include "LayoutStrategy.h"

#include "../../../rendering/TextRenderer.h"
#include "WString.h"
#include "WordProvider.h"

#ifdef ARDUINO
#include <Arduino.h>
#endif

#include <cmath>

std::vector<LayoutStrategy::Word> LayoutStrategy::getNextLine(WordProvider& provider, TextRenderer& renderer,
                                                              int16_t maxWidth, bool& isParagraphEnd) {
  isParagraphEnd = false;

  std::vector<LayoutStrategy::Word> line;
  int16_t currentWidth = 0;

  while (provider.hasNextWord()) {
    int wordStartIndex = provider.getCurrentIndex();
    String text = provider.getNextWord();
    int16_t bx = 0, by = 0;
    uint16_t bw = 0, bh = 0;
    renderer.getTextBounds(text.c_str(), 0, 0, &bx, &by, &bw, &bh);
    LayoutStrategy::Word word{text, static_cast<int16_t>(bw)};

    // Check for breaks - breaks are returned as special words
    if (word.text == String("\n")) {
      isParagraphEnd = true;
      break;
    }
    if (word.text[0] == ' ') {
      continue;
    }

    // Calculate space needed for this word
    int16_t spaceNeeded = currentWidth > 0 ? spaceWidth_ + word.width : word.width;

    if (currentWidth + spaceNeeded > maxWidth) {
      // Word doesn't fit, try to split it at a hyphen
      int16_t availableWidth = maxWidth - currentWidth - spaceWidth_;
      std::vector<int> hyphenPositions = findHyphenPositions(word.text);
      int splitPos = findBestHyphenSplitForward(word.text, hyphenPositions, availableWidth, renderer);

      if (splitPos >= 0) {
        // Successfully found a split position - add first part (up to and including the hyphen)
        String firstPart = word.text.substring(0, splitPos + 1);
        int16_t bx2 = 0, by2 = 0;
        uint16_t bw2 = 0, bh2 = 0;
        renderer.getTextBounds(firstPart.c_str(), 0, 0, &bx2, &by2, &bw2, &bh2);
        line.push_back({firstPart, static_cast<int16_t>(bw2)});

        // Move provider position to right after the hyphen
        provider.setPosition(wordStartIndex + splitPos + 1);
        break;
      } else if (currentWidth > 0) {
        // Can't split, put it back and end line
        provider.ungetWord();
        break;
      }
    } else {
      // Word fits, add to line
      line.push_back(word);
      currentWidth += spaceNeeded;
    }
  }

  return line;
}

std::vector<LayoutStrategy::Word> LayoutStrategy::getPrevLine(WordProvider& provider, TextRenderer& renderer,
                                                              int16_t maxWidth, bool& isParagraphEnd) {
  isParagraphEnd = false;

  std::vector<LayoutStrategy::Word> line;
  int16_t currentWidth = 0;

  while (provider.getCurrentIndex() > 0) {
    int wordEndIndex = provider.getCurrentIndex();
    String text = provider.getPrevWord();
    int wordStartIndex = provider.getCurrentIndex();

    // Measure the rendered width using the renderer
    int16_t bx = 0, by = 0;
    uint16_t bw = 0, bh = 0;
    renderer.getTextBounds(text.c_str(), 0, 0, &bx, &by, &bw, &bh);
    LayoutStrategy::Word word{text, static_cast<int16_t>(bw)};

    // Check for breaks - breaks are returned as special words
    if (word.text == String("\n")) {
      isParagraphEnd = true;
      break;
    }
    if (word.text[0] == ' ') {
      continue;
    }

    // Try to add word to the beginning of the line
    int16_t spaceNeeded = currentWidth > 0 ? spaceWidth_ + word.width : word.width;
    if (currentWidth + spaceNeeded > maxWidth) {
      // Word doesn't fit, try to split it at a hyphen
      int16_t availableWidth = maxWidth - currentWidth - spaceWidth_;
      std::vector<int> hyphenPositions = findHyphenPositions(word.text);
      int splitPos = findBestHyphenSplitBackward(word.text, hyphenPositions, availableWidth, renderer);

      if (splitPos >= 0) {
        // Successfully found a split position - add second part (after the hyphen)
        String secondPart = word.text.substring(splitPos + 1, word.text.length());
        int16_t bx2 = 0, by2 = 0;
        uint16_t bw2 = 0, bh2 = 0;
        renderer.getTextBounds(secondPart.c_str(), 0, 0, &bx2, &by2, &bw2, &bh2);
        line.insert(line.begin(), {secondPart, static_cast<int16_t>(bw2)});

        // Move provider position to right after the hyphen
        provider.setPosition(wordStartIndex + splitPos + 1);
        break;
      } else if (currentWidth > 0) {
        // Can't split, put it back
        provider.ungetWord();
        break;
      }
    } else {
      line.insert(line.begin(), word);
      currentWidth += spaceNeeded;
    }
  }

  return line;
}

int LayoutStrategy::getPreviousPageStart(WordProvider& provider, TextRenderer& renderer, const LayoutConfig& config,
                                         int currentStartPosition) {
  // Save current provider state
  int savedPosition = provider.getCurrentIndex();

  // Set provider to the end of current page
  provider.setPosition(currentStartPosition);
  String word = provider.getPrevWord();
  if (word != String("\n")) {
    provider.ungetWord();
  }

  const int16_t maxWidth = config.pageWidth - config.marginLeft - config.marginRight;
  renderer.getTextBounds(" ", 0, 0, nullptr, nullptr, &spaceWidth_, nullptr);

  // Calculate how many lines fit on the screen
  const int16_t availableHeight = config.pageHeight - config.marginTop - config.marginBottom;
  const int maxLines = ceil(availableHeight / (double)config.lineHeight);

  // Go backwards, laying out lines in reverse order
  // Stop after reaching a paragraph break
  int linesBack = 0;
  // position after going backwards to the paragraph break
  int positionAtBreak = 0;

  while (provider.getCurrentIndex() > 0) {
    bool isParagraphEnd;
    std::vector<LayoutStrategy::Word> line = getPrevLine(provider, renderer, maxWidth, isParagraphEnd);

    linesBack++;

    // Stop if we hit a paragraph break
    if (isParagraphEnd && linesBack >= maxLines) {
      positionAtBreak = provider.getCurrentIndex() + 1;
      provider.setPosition(positionAtBreak);
      break;
    }
  }

  int linesMoved = 0;

  // Move forward the difference between screen lines and lines we moved back
  int linesToMoveForward = linesBack - maxLines;
  if (linesToMoveForward > 0) {
    while (provider.hasNextWord() && linesMoved < linesToMoveForward) {
      bool dummyParagraphEnd;
      std::vector<LayoutStrategy::Word> line = getNextLine(provider, renderer, maxWidth, dummyParagraphEnd);
      linesMoved++;
    }
  }

  // The current position is where the previous page starts
  int previousPageStart = provider.getCurrentIndex();

  // Restore provider state
  provider.setPosition(savedPosition);

  return previousPageStart;
}

std::vector<int> LayoutStrategy::findHyphenPositions(const String& word) {
  std::vector<int> positions;

  for (int i = 0; i < word.length(); i++) {
    if (word[i] == '-') {
      positions.push_back(i);
    }
  }

  return positions;
}

int LayoutStrategy::findBestHyphenSplitForward(const String& word, const std::vector<int>& hyphenPositions,
                                               int16_t availableWidth, TextRenderer& renderer) {
  // Find the last (rightmost) hyphen position where the first part fits
  int splitPos = -1;

  for (int i = 0; i < hyphenPositions.size(); i++) {
    int pos = hyphenPositions[i];
    String candidate = word.substring(0, pos + 1);
    int16_t bx = 0, by = 0;
    uint16_t bw = 0, bh = 0;
    renderer.getTextBounds(candidate.c_str(), 0, 0, &bx, &by, &bw, &bh);

    if (bw <= availableWidth) {
      splitPos = pos;  // This hyphen works, keep looking for a later one
    } else {
      break;  // Too wide, stop searching
    }
  }

  return splitPos;
}

int LayoutStrategy::findBestHyphenSplitBackward(const String& word, const std::vector<int>& hyphenPositions,
                                                int16_t availableWidth, TextRenderer& renderer) {
  // Find the earliest (leftmost) hyphen position where the second part fits
  int splitPos = -1;

  for (int i = hyphenPositions.size() - 1; i >= 0; i--) {
    int pos = hyphenPositions[i];
    String candidate = word.substring(pos + 1, word.length());
    int16_t bx = 0, by = 0;
    uint16_t bw = 0, bh = 0;
    renderer.getTextBounds(candidate.c_str(), 0, 0, &bx, &by, &bw, &bh);

    if (bw <= availableWidth) {
      splitPos = pos;  // This hyphen works, keep looking for an earlier one
    } else {
      break;  // Too wide, stop searching
    }
  }

  return splitPos;
}

std::vector<LayoutStrategy::Word> LayoutStrategy::test_getPrevLine(WordProvider& provider, TextRenderer& renderer,
                                                                   int16_t maxWidth, bool& isParagraphEnd) {
  return getPrevLine(provider, renderer, maxWidth, isParagraphEnd);
}

std::vector<LayoutStrategy::Word> LayoutStrategy::test_getNextLineDefault(WordProvider& provider,
                                                                          TextRenderer& renderer, int16_t maxWidth,
                                                                          bool& isParagraphEnd) {
  return getNextLine(provider, renderer, maxWidth, isParagraphEnd);
}

int LayoutStrategy::test_getPreviousPageStart(WordProvider& provider, TextRenderer& renderer,
                                              const LayoutConfig& config, int currentStartPosition) {
  return getPreviousPageStart(provider, renderer, config, currentStartPosition);
}
