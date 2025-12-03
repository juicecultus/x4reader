#include "GreedyLayoutStrategy.h"

#include "../../../rendering/TextRenderer.h"
#include "WString.h"
#include "WordProvider.h"

#ifdef ARDUINO
#include <Arduino.h>
#endif
#ifndef ARDUINO
#include "platform_stubs.h"
#endif
#include <cmath>

GreedyLayoutStrategy::GreedyLayoutStrategy() {}

GreedyLayoutStrategy::~GreedyLayoutStrategy() {}

int GreedyLayoutStrategy::layoutText(WordProvider& provider, TextRenderer& renderer, const LayoutConfig& config,
                                     bool disableRendering) {
  const int16_t maxWidth = config.pageWidth - config.marginLeft - config.marginRight;
  const int16_t x = config.marginLeft;
  int16_t y = config.marginTop;
  const int16_t maxY = config.pageHeight - config.marginBottom;
  const TextAlignment alignment = config.alignment;

  // Measure space width using renderer
  renderer.getTextBounds(" ", 0, 0, nullptr, nullptr, &spaceWidth_, nullptr);

  int startIndex = provider.getCurrentIndex();
  while (y < maxY) {
    bool isParagraphEnd = false;
    std::vector<LayoutStrategy::Word> line = getNextLine(provider, renderer, maxWidth, isParagraphEnd);

    // Render the line (unless rendering is disabled)
    if (!disableRendering) {
      renderLine(line, renderer, x, y, maxWidth, alignment);
    }

    y += config.lineHeight;
  }

  int endIndex = provider.getCurrentIndex();
  // reset the provider to the start index
  provider.setPosition(startIndex);

  return endIndex;
}

void GreedyLayoutStrategy::renderLine(const std::vector<LayoutStrategy::Word>& line, TextRenderer& renderer, int16_t x,
                                      int16_t y, int16_t maxWidth, TextAlignment alignment) {
  if (line.empty()) {
    return;
  }

  // Calculate line width
  int16_t lineWidth = 0;
  for (size_t i = 0; i < line.size(); i++) {
    lineWidth += line[i].width;
    if (i < line.size() - 1) {
      lineWidth += spaceWidth_;
    }
  }

  int16_t xPos = x;
  if (alignment == ALIGN_CENTER) {
    xPos = x + (maxWidth - lineWidth) / 2;
  } else if (alignment == ALIGN_RIGHT) {
    xPos = x + maxWidth - lineWidth;
  }

  int16_t currentX = xPos;
  for (size_t i = 0; i < line.size(); i++) {
    renderer.setCursor(currentX, y);
    renderer.print(line[i].text);
    currentX += line[i].width;
    if (i < line.size() - 1) {
      currentX += spaceWidth_;
    }
  }

  return;
}

std::vector<LayoutStrategy::Word> GreedyLayoutStrategy::test_getNextLine(WordProvider& provider, TextRenderer& renderer,
                                                                         int16_t maxWidth, bool& isParagraphEnd) {
  return getNextLine(provider, renderer, maxWidth, isParagraphEnd);
}
