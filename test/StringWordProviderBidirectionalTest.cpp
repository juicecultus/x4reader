#include <iostream>
#include <string>

#include "../src/ui/screens/textview/FileWordProvider.h"
#include "../src/ui/screens/textview/StringWordProvider.h"
#include "test_config.h"
#include "test_utils.h"

// Provide a minimal TextRenderer type only so we can call the real
// StringWordProvider::getNextWord signature. The implementation of
// StringWordProvider no longer depends on the renderer, so this type
// can be empty for host tests.
class TextRenderer {};

int main(int argc, char** argv) {
  TestUtils::TestRunner runner("StringWordProvider Bidirectional Test");

  std::string path;
  if (argc >= 2)
    path = argv[1];
  else
    path = TestConfig::DEFAULT_TEST_FILE;

  std::string content = TestUtils::readFile(path);
  if (content.empty()) {
    std::cerr << "Failed to open '" << path << "'\n";
    return 2;
  }

  std::string expected = TestUtils::normalizeLineEndings(content);

  // Convert std::string to Arduino-like String via implicit constructor
  String s(expected.c_str());
  StringWordProvider provider(s);
  TextRenderer renderer;

  // Test 1: String forward reconstruction
  std::string rebuilt;
  while (provider.hasNextWord()) {
    std::string w = provider.getNextWord().c_str();
    if (w.length() == 0)
      break;
    // Append token exactly as returned. w may contain whitespace tokens
    rebuilt += w;
  }
  runner.expectEqual(expected, rebuilt, "String forward: reconstructed text equals original", true);

  // Test 2: String backward reconstruction using getPrevWord starting from end
  provider.setPosition(static_cast<int>(content.length()));
  std::string rebuiltBack;
  while (true) {
    std::string w = provider.getPrevWord().c_str();
    if (w.length() == 0)
      break;
    // Prepend token since getPrevWord returns tokens in reverse order
    rebuiltBack.insert(0, w);
  }
  runner.expectEqual(expected, rebuiltBack, "String backward: reconstructed text equals original", true);

  // Test 3: FileWordProvider forward
  FileWordProvider fileProvider(path.c_str(), TestConfig::FILE_BUFFER_SIZE);
  std::string rebuiltFile;
  while (fileProvider.hasNextWord()) {
    std::string w = fileProvider.getNextWord().c_str();
    if (w.length() == 0)
      break;
    rebuiltFile += w;
  }
  runner.expectEqual(expected, rebuiltFile, "File forward: reconstructed text equals original", true);

  // Test 4: FileWordProvider backward
  fileProvider.setPosition(static_cast<int>(content.length()));
  std::string rebuiltFileBack;
  while (true) {
    std::string w = fileProvider.getPrevWord().c_str();
    if (w.length() == 0)
      break;
    rebuiltFileBack.insert(0, w);
  }
  runner.expectEqual(expected, rebuiltFileBack, "File backward: reconstructed text equals original", true);

  // Test 5 & 6: Compare outputs of String and File providers
  runner.expectEqual(rebuilt, rebuiltFile, "Providers match forward: String and File produce same output");
  runner.expectEqual(rebuiltBack, rebuiltFileBack, "Providers match backward: String and File produce same output");

  // Test 7: Step-by-step comparison of providers
  StringWordProvider stringProvider2(s);
  FileWordProvider fileProvider2(path.c_str(), TestConfig::FILE_BUFFER_SIZE);
  bool stepPass = true;
  std::string errorMsg;

  while (stringProvider2.hasNextWord() && fileProvider2.hasNextWord()) {
    String sWord = stringProvider2.getNextWord();
    String fWord = fileProvider2.getNextWord();
    if (sWord != fWord) {
      errorMsg = "Words differ: '" + std::string(sWord.c_str()) + "' vs '" + std::string(fWord.c_str()) + "'";
      stepPass = false;
      break;
    }
  }

  if (stepPass && (stringProvider2.hasNextWord() || fileProvider2.hasNextWord())) {
    errorMsg = "One provider has more words";
    stepPass = false;
  }

  runner.expectTrue(stepPass, "Step-by-step forward: providers return identical words", errorMsg);

  return runner.allPassed() ? 0 : 1;
}
