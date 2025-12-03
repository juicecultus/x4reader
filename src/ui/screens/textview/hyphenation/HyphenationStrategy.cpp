#include "HyphenationStrategy.h"

#include "GermanHyphenation.h"

// Implementation of the base class method
std::vector<int> HyphenationStrategy::findHyphenPositions(const std::string& word, size_t minWordLength,
                                                          size_t minFragmentLength) {
  std::vector<int> positions;

  // First, find existing hyphens in the text
  for (size_t i = 0; i < word.length(); i++) {
    if (word[i] == '-') {
      positions.push_back(static_cast<int>(i));
    }
  }

  // Add algorithmic hyphenation positions for words without existing hyphens
  if (positions.empty()) {
    // Use the language-specific hyphenation strategy
    std::vector<size_t> algorithmicPositions = hyphenate(word, minWordLength, minFragmentLength);

    // Store as negative values to indicate these are algorithmic positions
    // (need hyphen insertion). Offset by -1 so position 0 becomes -1, etc.
    for (size_t bytePos : algorithmicPositions) {
      positions.push_back(-(static_cast<int>(bytePos) + 1));
    }
  }

  return positions;
}

/**
 * German hyphenation strategy implementation
 */
class GermanHyphenationStrategy : public HyphenationStrategy {
 public:
  std::vector<size_t> hyphenate(const std::string& word, size_t minWordLength = 6,
                                size_t minFragmentLength = 3) override {
    // Only hyphenate words that meet minimum length requirement
    if (word.length() < minWordLength) {
      return std::vector<size_t>();
    }

    // Get hyphenation positions from German algorithm
    std::vector<size_t> positions = GermanHyphenation::hyphenate(word);

    // Filter out positions that would create fragments that are too short
    std::vector<size_t> filtered;
    for (size_t pos : positions) {
      if (pos >= minFragmentLength && pos <= word.length() - minFragmentLength) {
        filtered.push_back(pos);
      }
    }

    return filtered;
  }

  Language getLanguage() const override {
    return Language::GERMAN;
  }
};

/**
 * Factory function implementation
 */
HyphenationStrategy* createHyphenationStrategy(Language language) {
  switch (language) {
    case Language::GERMAN:
      return new GermanHyphenationStrategy();
    case Language::BASIC:
      return new BasicHyphenation();
    case Language::ENGLISH:
      // TODO: Implement English hyphenation
      return new NoHyphenation();
    case Language::NONE:
    default:
      return new NoHyphenation();
  }
}
