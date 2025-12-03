#ifndef GERMAN_HYPHENATION_H
#define GERMAN_HYPHENATION_H

#include <string>
#include <vector>

namespace GermanHyphenation {

/**
 * Find hyphenation positions in a German word.
 * Returns byte positions where hyphens can be inserted.
 */
std::vector<size_t> hyphenate(const std::string& word);

/**
 * Insert hyphens at specified positions in a word.
 */
std::string insertHyphens(const std::string& word, const std::vector<size_t>& positions);

/**
 * Extract hyphen positions from an already-hyphenated word.
 */
std::vector<size_t> positionsFromAnnotated(const std::string& annotated);

}  // namespace GermanHyphenation

#endif
