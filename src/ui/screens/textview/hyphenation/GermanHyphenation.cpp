#include "GermanHyphenation.h"

#include <algorithm>
#include <codecvt>
#include <cwctype>
#include <locale>
#include <unordered_set>

namespace {

std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> converter;

char32_t toLowerGerman(char32_t c) {
  switch (c) {
    case U'Ä':
      return U'ä';
    case U'Ö':
      return U'ö';
    case U'Ü':
      return U'ü';
    case U'ẞ':
      return U'ß';
    default:
      return std::towlower(c);
  }
}

bool isLetter(char32_t c) {
  return (c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z') || c == U'ä' || c == U'ö' || c == U'ü' || c == U'Ä' ||
         c == U'Ö' || c == U'Ü' || c == U'ß';
}

bool isVowel(char32_t c) {
  c = toLowerGerman(c);
  switch (c) {
    case U'a':
    case U'e':
    case U'i':
    case U'o':
    case U'u':
    case U'ä':
    case U'ö':
    case U'ü':
    case U'y':
      return true;
    default:
      return false;
  }
}

bool isConsonant(char32_t c) {
  return isLetter(c) && !isVowel(c);
}

bool isAllowedOnset(const std::u32string& onset) {
  static const std::vector<std::u32string> allowed = {
      U"b",  U"c",    U"d",    U"f",    U"g",    U"h",    U"j",   U"k",   U"l",   U"m",  U"n",   U"p",  U"q",
      U"r",  U"s",    U"t",    U"v",    U"w",    U"z",    U"ch",  U"pf",  U"ph",  U"qu", U"sch", U"sp", U"st",
      U"sk", U"kl",   U"kn",   U"kr",   U"pl",   U"pr",   U"tr",  U"dr",  U"gr",  U"gl", U"br",  U"bl", U"fr",
      U"fl", U"schl", U"schm", U"schn", U"schr", U"schw", U"spr", U"spl", U"str", U"th"};

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
    result.push_back(toLowerGerman(c));
  }
  return result;
}

}  // namespace

namespace GermanHyphenation {

std::vector<size_t> hyphenate(const std::string& word) {
  std::u32string wide = converter.from_bytes(word);
  std::u32string lower = toLower(wide);

  std::vector<size_t> positions;
  std::vector<size_t> vowelIndices;
  for (size_t i = 0; i < lower.size(); ++i) {
    if (isVowel(lower[i])) {
      vowelIndices.push_back(i);
    }
  }

  if (vowelIndices.size() < 2) {
    return positions;
  }

  auto isInseparablePair = [](const std::u32string& pair) {
    static const std::vector<std::u32string> pairs = {U"ch", U"ck", U"ph", U"qu", U"tz"};
    for (const auto& p : pairs) {
      if (pair == p) {
        return true;
      }
    }
    return false;
  };

  for (size_t i = 0; i + 1 < vowelIndices.size(); ++i) {
    size_t leftVowel = vowelIndices[i];
    size_t rightVowel = vowelIndices[i + 1];

    if (rightVowel <= leftVowel + 1) {
      continue;  // Diphthong or adjacent vowels
    }

    size_t consonantCount = rightVowel - leftVowel - 1;
    size_t clusterStart = leftVowel + 1;
    size_t clusterEnd = rightVowel;  // exclusive
    size_t boundary = 0;

    std::u32string cluster(lower.begin() + clusterStart, lower.begin() + clusterEnd);

    if (cluster.size() >= 3 && matchesCluster(cluster, 0, U"sch")) {
      boundary = clusterStart;  // Keep "sch" together
    }

    if (boundary == 0 && consonantCount == 2) {
      if (isInseparablePair(cluster)) {
        boundary = clusterEnd;  // keep digraph with the left syllable
      }
    }

    if (boundary == 0 && isAllowedOnset(cluster)) {
      boundary = clusterStart;
    }

    if (boundary == 0 && consonantCount >= 2) {
      for (size_t split = 1; split < cluster.size(); ++split) {
        std::u32string onset(cluster.begin() + split, cluster.end());
        if (isAllowedOnset(onset)) {
          boundary = clusterStart + split;
          break;
        }
      }
    }

    if (boundary == 0) {
      if (consonantCount == 1) {
        boundary = clusterStart;
      } else if (consonantCount == 2) {
        std::u32string pair = cluster;
        if (isInseparablePair(pair)) {
          boundary = clusterEnd;
        } else {
          boundary = clusterStart + 1;
        }
      } else {  // consonantCount >= 3
        std::u32string lastTwo(lower.begin() + clusterEnd - 2, lower.begin() + clusterEnd);
        if (isInseparablePair(lastTwo)) {
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

}  // namespace GermanHyphenation
