#include "EnglishHyphenation.h"

#include <algorithm>
#include <codecvt>
#include <cwctype>
#include <locale>
#include <unordered_set>

namespace {

std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> converter;

char32_t toLowerEnglish(char32_t c) {
  return std::towlower(c);
}

bool isLetter(char32_t c) {
  return (c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z');
}

bool isVowel(char32_t c) {
  c = toLowerEnglish(c);
  switch (c) {
    case U'a':
    case U'e':
    case U'i':
    case U'o':
    case U'u':
      return true;
    default:
      return false;
  }
}

// Y is treated as a vowel when it appears after a consonant and before another consonant or end
bool isVowelInContext(char32_t c, size_t pos, const std::u32string& word) {
  if (isVowel(c)) return true;

  char32_t lower = toLowerEnglish(c);
  if (lower == U'y' && pos > 0) {
    // Y acts as vowel if preceded by consonant
    char32_t prev = toLowerEnglish(word[pos - 1]);
    if (isLetter(prev) && !isVowel(prev)) {
      return true;
    }
  }
  return false;
}

bool isConsonant(char32_t c) {
  return isLetter(c) && !isVowel(c);
}

bool isAllowedOnset(const std::u32string& onset) {
  // Common English syllable onsets (consonant clusters that can start a syllable)
  static const std::vector<std::u32string> allowed = {
      // Single consonants
      U"b", U"c", U"d", U"f", U"g", U"h", U"j", U"k", U"l", U"m",
      U"n", U"p", U"q", U"r", U"s", U"t", U"v", U"w", U"x", U"y", U"z",
      // Two-consonant clusters
      U"bl", U"br", U"ch", U"cl", U"cr", U"dr", U"dw", U"fl", U"fr",
      U"gh", U"gl", U"gn", U"gr", U"kn", U"ph", U"pl", U"pr", U"qu",
      U"sc", U"sh", U"sk", U"sl", U"sm", U"sn", U"sp", U"sq", U"st",
      U"sw", U"th", U"tr", U"tw", U"wh", U"wr",
      // Three-consonant clusters
      U"chr", U"sch", U"scr", U"shr", U"sph", U"spl", U"spr", U"squ",
      U"str", U"thr"
  };

  return std::find(allowed.begin(), allowed.end(), onset) != allowed.end();
}

bool matchesCluster(const std::u32string& text, size_t start, const std::u32string& cluster) {
  if (start + cluster.size() > text.size()) {
    return false;
  }
  return std::equal(cluster.begin(), cluster.end(), text.begin() + start);
}

std::u32string toLower(const std::u32string& input) {
  std::u32string result;
  result.reserve(input.size());
  for (char32_t c : input) {
    result.push_back(toLowerEnglish(c));
  }
  return result;
}

}  // namespace

namespace EnglishHyphenation {

std::vector<size_t> hyphenate(const std::string& word) {
  std::u32string wide = converter.from_bytes(word);
  std::u32string lower = toLower(wide);

  std::vector<size_t> positions;
  std::vector<size_t> vowelIndices;

  // Find vowel positions (including Y when it acts as vowel)
  for (size_t i = 0; i < lower.size(); ++i) {
    if (isVowelInContext(lower[i], i, lower)) {
      vowelIndices.push_back(i);
    }
  }

  // Need at least 2 vowels to hyphenate
  if (vowelIndices.size() < 2) {
    return positions;
  }

  // Inseparable consonant pairs that should stay together
  auto isInseparablePair = [](const std::u32string& pair) {
    static const std::vector<std::u32string> pairs = {
        U"ch", U"ck", U"gh", U"gn", U"kn", U"ph", U"sh", U"th", U"wh", U"wr"
    };
    for (const auto& p : pairs) {
      if (pair == p) {
        return true;
      }
    }
    return false;
  };

  auto isDoubleConsonant = [](const std::u32string& pair) {
    return pair.size() == 2 && pair[0] == pair[1] && isConsonant(pair[0]);
  };

  // Process consonant clusters between vowels
  for (size_t i = 0; i + 1 < vowelIndices.size(); ++i) {
    size_t leftVowel = vowelIndices[i];
    size_t rightVowel = vowelIndices[i + 1];

    // Skip if vowels are adjacent (diphthong or hiatus)
    if (rightVowel <= leftVowel + 1) {
      continue;
    }

    size_t consonantCount = rightVowel - leftVowel - 1;
    size_t clusterStart = leftVowel + 1;
    size_t clusterEnd = rightVowel;  // exclusive
    size_t boundary = 0;

    std::u32string cluster(lower.begin() + clusterStart, lower.begin() + clusterEnd);

    // Special handling for common patterns

    // Double consonants: split between them (run-ning, let-ter)
    if (boundary == 0 && consonantCount == 2) {
      if (isDoubleConsonant(cluster)) {
        boundary = clusterStart + 1;
      }
    }

    // Inseparable pairs stay on the right (fa-ther, gra-phic)
    if (boundary == 0 && consonantCount == 2) {
      if (isInseparablePair(cluster)) {
        boundary = clusterStart;
      }
    }

    // Try to find valid onset by checking if entire cluster is allowed
    if (boundary == 0 && isAllowedOnset(cluster)) {
      boundary = clusterStart;
    }

    // Try to find the largest valid onset from the right
    if (boundary == 0 && consonantCount >= 2) {
      for (size_t split = 1; split < cluster.size(); ++split) {
        std::u32string onset(cluster.begin() + split, cluster.end());
        if (isAllowedOnset(onset)) {
          // Check if what remains on the left is a valid coda
          std::u32string coda(cluster.begin(), cluster.begin() + split);
          // Most single consonants and some pairs are valid codas
          if (coda.size() <= 2) {
            boundary = clusterStart + split;
            break;
          }
        }
      }
    }

    // Fallback rules
    if (boundary == 0) {
      if (consonantCount == 1) {
        // Single consonant goes with following vowel (o-pen, a-ble)
        boundary = clusterStart;
      } else if (consonantCount == 2) {
        std::u32string pair = cluster;
        if (isInseparablePair(pair)) {
          boundary = clusterStart;  // Keep pair on right
        } else {
          boundary = clusterStart + 1;  // Split in middle
        }
      } else {  // consonantCount >= 3
        // Keep last consonant(s) with right syllable
        std::u32string lastTwo(lower.begin() + clusterEnd - 2, lower.begin() + clusterEnd);
        if (isInseparablePair(lastTwo) || isAllowedOnset(lastTwo)) {
          boundary = clusterEnd - 2;
        } else {
          boundary = clusterEnd - 1;
        }
      }
    }

    if (boundary > 0 && boundary < wide.size()) {
      positions.push_back(boundary);
    }
  }

  // Convert character positions to byte positions in UTF-8
  std::vector<size_t> bytePositions;
  bytePositions.reserve(positions.size());

  for (size_t charPos : positions) {
    // Count bytes up to the character position
    size_t bytePos = 0;
    size_t currentChar = 0;
    const char* str = word.c_str();

    // Count through UTF-8 characters
    while (currentChar < charPos && str[bytePos] != '\0') {
      // Skip to the next UTF-8 character by advancing past continuation bytes
      bytePos++;
      // Skip continuation bytes (10xxxxxx pattern)
      while (str[bytePos] != '\0' && (str[bytePos] & 0xC0) == 0x80) {
        bytePos++;
      }
      currentChar++;
    }

    bytePositions.push_back(bytePos);
  }
  return bytePositions;
}

std::string insertHyphens(const std::string& word, const std::vector<size_t>& positions) {
  std::u32string wide = converter.from_bytes(word);
  std::u32string result;
  result.reserve(wide.size() + positions.size());

  std::unordered_set<size_t> posSet(positions.begin(), positions.end());
  for (size_t i = 0; i < wide.size(); ++i) {
    if (posSet.find(i) != posSet.end()) {
      result.push_back(U'-');
    }
    result.push_back(wide[i]);
  }

  return converter.to_bytes(result);
}

std::vector<size_t> positionsFromAnnotated(const std::string& annotated) {
  std::u32string wide = converter.from_bytes(annotated);
  std::vector<size_t> positions;
  size_t wordIndex = 0;
  for (char32_t c : wide) {
    if (c == U'-') {
      positions.push_back(wordIndex);
    } else {
      wordIndex++;
    }
  }
  return positions;
}

}  // namespace EnglishHyphenation
